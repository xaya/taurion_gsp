/* Utility program to read the raw map data files and convert them into a
   format that is compact and easy to read at runtime.  The processed data
   is then used by the BaseMap class.  */

#include "config.h"

#include "dataio.hpp"
#include "tiledata.hpp"

#include "hexagonal/coord.hpp"
#include "hexagonal/rangemap.hpp"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <unordered_map>
#include <vector>

DEFINE_string (obstacle_input, "",
               "The file with input obstacle data");
DEFINE_string (region_input, "",
               "The file with input data for the region map");
DEFINE_string (code_output, "",
               "The output file for processed data as C++ code");
DEFINE_string (obstacle_output, "",
               "The output file for raw obstacle layer data");
DEFINE_string (region_map_output, "",
               "The output file for raw region map data");
DEFINE_string (region_xcoord_output, "",
               "The output file for x coordinates in compact region data");
DEFINE_string (region_ids_output, "",
               "The output file for IDs in the compact region data");

namespace pxd
{
namespace
{

/** L1 range around the origin that is enough to hold all tiles.  */
constexpr HexCoord::IntT FULL_L1RANGE = 7000;

/** Number of bools to pack into each character for the bit vector.  */
constexpr int BITS = 8;

/**
 * Simple helper struct that keeps track of minimum and maximum seen values.
 */
struct MinMax
{

  /** True if we already have seen any value.  */
  bool initialised = false;

  /** Minimum value seen.  */
  int minVal;
  /** Maximum value seen.  */
  int maxVal;

  MinMax () = default;
  MinMax (const MinMax&) = default;
  MinMax& operator= (const MinMax&) = default;

  void
  Update (const int cur)
  {
    if (!initialised)
      {
        initialised = true;
        minVal = cur;
        maxVal = cur;
        return;
      }

    minVal = std::min (minVal, cur);
    maxVal = std::max (maxVal, cur);
  }

};

bool
operator== (const MinMax& a, const MinMax& b)
{
  CHECK (a.initialised && b.initialised);
  return a.minVal == b.minVal && a.maxVal == b.maxVal;
}

bool
operator!= (const MinMax& a, const MinMax& b)
{
  return !(a == b);
}

std::ostream&
operator<< (std::ostream& out, const MinMax& range)
{
  out << range.minVal << " ... " << range.maxVal;
  return out;
}

/**
 * The ranges of seen coordinates for per-tile data.
 */
class CoordRanges
{

private:

  /** The minimum and maximum seen row values.  */
  MinMax rowRange;

  /** For each row, the range of seen column values.  */
  std::unordered_map<int, MinMax> columnRange;

public:

  CoordRanges () = default;

  CoordRanges (CoordRanges&&) = default;
  CoordRanges& operator= (CoordRanges&&) = default;

  CoordRanges (const CoordRanges&) = delete;
  void operator= (const CoordRanges&) = delete;

  const MinMax&
  GetRowRange () const
  {
    return rowRange;
  }

  const MinMax&
  GetColumnRange (const int y) const
  {
    return columnRange.at (y);
  }

  /**
   * Resets this to the "default" / empty state.
   */
  void
  Clear ()
  {
    rowRange = MinMax ();
    columnRange.clear ();
  }

  /**
   * Updates the ranges for a newly seen coordinate.
   */
  void
  Update (const HexCoord& c)
  {
    rowRange.Update (c.GetY ());
    columnRange[c.GetY ()].Update (c.GetX ());
  }

  /**
   * Writes out C++ code that defines the coordinate ranges in static
   * constants.
   */
  void
  WriteCode (std::ostream& out) const
  {
    LOG (INFO) << "Writing coordinate ranges as C++ code...";

    out << "const int minY = " << rowRange.minVal << ";" << std::endl;
    out << "const int maxY = " << rowRange.maxVal << ";" << std::endl;

    out << "const int minX[] = {" << std::endl;
    for (int y = rowRange.minVal; y <= rowRange.maxVal; ++y)
      {
        const auto mit = columnRange.find (y);
        CHECK (mit != columnRange.end ()) << "No column data for row " << y;
        out << "  " << mit->second.minVal << "," << std::endl;
      }
    out << "}; // minX" << std::endl;

    out << "const int maxX[] = {" << std::endl;
    for (int y = rowRange.minVal; y <= rowRange.maxVal; ++y)
      out << "  " << columnRange.at (y).maxVal << "," << std::endl;
    out << "}; // maxX" << std::endl;

    out << R"(
      #define CHECK_YARRAY_LEN(var) \
          static_assert (sizeof (var) / sizeof (var[0]) == (maxY - minY + 1), \
                         #var " has unexpected size")

      CHECK_YARRAY_LEN (minX);
      CHECK_YARRAY_LEN (maxX);
    )";
  }

  friend bool
  operator== (const CoordRanges& a, const CoordRanges& b)
  {
    if (a.rowRange != b.rowRange)
      return false;

    if (a.columnRange.size () != b.columnRange.size ())
      return false;

    for (const auto& aColRange : a.columnRange)
      {
        const auto bIt = b.columnRange.find (aColRange.first);
        if (bIt == b.columnRange.end ())
          return false;
        if (aColRange.second != bIt->second)
          return false;
      }

    return true;
  }

};

/**
 * Base class for processing per-tile data.  The data is assumed to be
 * "square", i.e. with y coordinates in some range and then x in another
 * range (dependent on y).
 *
 * The actual data stored for a tile and the logic to output that data again
 * is not included here but has to be done in subclasses.
 */
class PerTileData
{

private:

  /** Whether or not data has already been read.  */
  bool initialised = false;

  /** Coordinate ranges seen.  */
  CoordRanges ranges;

protected:

  PerTileData () = default;

  PerTileData (const PerTileData&) = delete;
  void operator= (const PerTileData&) = delete;

  /**
   * Reads data for a tile with the given coordinates from the input stream
   * and stores it internally.
   */
  virtual void ReadTile (const HexCoord& coord, std::istream& in) = 0;

public:

  const CoordRanges&
  GetRanges () const
  {
    return ranges;
  }

  CoordRanges&&
  MoveRanges ()
  {
    return std::move (ranges);
  }

  /**
   * Reads in the data from the input binary stream.  The format of that data
   * file is as follows (all little-endian 16-bit signed integers):
   *
   * 2 ints giving the rows/columns of the square map (N * M),
   * (N * M) entries follow, giving axial x, axial y and the specific
   * data per tile encoded in some other form.
   */
  void
  ReadInput (std::istream& in)
  {
    CHECK (!initialised);

    const size_t n = Read<int16_t> (in);
    const size_t m = Read<int16_t> (in);
    LOG (INFO)
        << "Reading " << n << " * " << m << " = " << (n * m) << " tiles";
    for (size_t i = 0; i < n * m; ++i)
      {
        const auto x = Read<int16_t> (in);
        const auto y = Read<int16_t> (in);
        const HexCoord c(x, y);
        ranges.Update (c);

        ReadTile (c, in);
      }

    LOG (INFO) << "Finished reading input data";
    LOG (INFO) << "Row range: " << ranges.GetRowRange ();
  }

};

/**
 * Helper class to generate a bit vector.  Individual bits can be passed to
 * it as booleans, and it builds up a vector of compact bytes that represent
 * those bits.  That vector can then be written in binary or otherwise
 * processed.
 */
class BitVectorBuilder
{

private:

  /** Vector of bytes being built.  */
  std::vector<unsigned char> data;

  /** Byte currently being built up.  */
  int currentByte = 0;
  /** Number of bits already included in currentByte.  */
  int numBits = 0;

  /** Set to true when the builder is "finalised".  */
  bool finalised = false;

public:

  BitVectorBuilder () = default;
  BitVectorBuilder (const BitVectorBuilder&) = delete;
  void operator= (const BitVectorBuilder&) = delete;

  /**
   * Appends a new bit to the vector.  This is only valid if the builder
   * has not yet been finalised.
   */
  void
  Append (const bool bit)
  {
    CHECK (!finalised);

    CHECK (numBits < BITS);
    if (bit)
      currentByte |= (1 << numBits);
    ++numBits;

    if (numBits == BITS)
      {
        data.push_back (currentByte);
        currentByte = 0;
        numBits = 0;
      }
  }

  /**
   * Finalises the builder.  After that, Append() can no longer be called,
   * but the data can be accessed.  This just makes sure that the current
   * "half filled" byte is written.
   */
  void
  Finalise ()
  {
    CHECK (!finalised);
    finalised = true;

    if (numBits > 0)
      data.push_back (currentByte);
  }

  /**
   * Returns a read-only view of the compacted byte data.
   */
  const std::vector<unsigned char>&
  GetData () const
  {
    CHECK (finalised);
    return data;
  }

};

/**
 * Holds the obstacle data for the base map.
 */
class ObstacleData : public PerTileData
{

private:

  /**
   * Possible values for the per-tile data stored.
   */
  enum class Passable : int8_t
  {
    UNINITIALISED = 0,
    PASSABLE,
    OBSTACLE,
  };

  /** Map of already read obstacle "tiles" from the raw input.  */
  RangeMap<Passable> tiles;

protected:

  void
  ReadTile (const HexCoord& coord, std::istream& in) override
  {
    auto& val = tiles.Access (coord);
    CHECK (val == Passable::UNINITIALISED)
        << "Duplicate tiles in obstacle data input for coordinate " << coord;

    const bool passable = Read<int16_t> (in);
    if (passable)
      val = Passable::PASSABLE;
    else
      val = Passable::OBSTACLE;
  }

public:

  ObstacleData ()
    : tiles(HexCoord (), FULL_L1RANGE, Passable::UNINITIALISED)
  {}

  ObstacleData (const ObstacleData&) = delete;
  void operator= (const ObstacleData&) = delete;

  /**
   * Writes the data out.  Metadata is written as generated C++ code and
   * the bit vectors themselves are written as binary data in a separate file.
   */
  void
  Write (std::ostream& codeOut, std::ostream& rawOut) const
  {
    LOG (INFO) << "Writing obstacle data...";
    codeOut << "namespace obstacles {" << std::endl;

    /* We write the raw bit vectors concatenated to the binary output and
       also store the offsets where each row starts in the data in the
       generated code.  */
    size_t offset = 0;
    codeOut << "const size_t bitDataOffsetForY[] = {" << std::endl;
    for (int y = GetRanges ().GetRowRange ().minVal;
         y <= GetRanges ().GetRowRange ().maxVal; ++y)
      {
        codeOut << "  " << offset << "," << std::endl;
        const auto& colRange = GetRanges ().GetColumnRange (y);

        BitVectorBuilder bits;
        for (int x = colRange.minVal; x <= colRange.maxVal; ++x)
          {
            const HexCoord c(x, y);
            const auto val = tiles.Get (c);

            switch (val)
              {
              case Passable::PASSABLE:
              case Passable::OBSTACLE:
                bits.Append (val == Passable::PASSABLE);
                break;

              case Passable::UNINITIALISED:
                LOG (FATAL) << "No passable data for tile " << c;
                break;

              default:
                LOG (FATAL)
                    << "Invalid passable value " << static_cast<int> (val)
                    << " for tile " << c;
                break;
              }
          }

        bits.Finalise ();
        const auto& data = bits.GetData ();

        rawOut.write (reinterpret_cast<const char*> (data.data ()),
                      data.size ());
        offset += data.size ();
      }
    codeOut << "}; // bitDataOffsetForY" << std::endl;
    codeOut << "CHECK_YARRAY_LEN (bitDataOffsetForY);" << std::endl;

    codeOut << "const size_t bitDataSize = " << offset << ";" << std::endl;

    codeOut << "} // namespace obstacles" << std::endl;
  }

};

/**
 * Holds and processes the tiles-to-region map.  The output is raw binary that
 * contains the region IDs (each in 24 bit) for all the tiles, with all
 * rows concatenated.
 *
 * The region map is also output in compact form:  There, for each row,
 * we "compress" contiguous blocks of the same region ID.  In other words,
 * we output two arrays:  One of x coordinates and one of corresponding
 * region IDs.  An entry (x, id) means that all tiles with a given y coordinate
 * and x coordinate between x (inclusive) and the next x (exclusive) have the
 * given region ID.  This compacts data massively, and still allows efficient
 * lookup using binary search over x.
 */
class RegionData : public PerTileData
{

private:

  /** The region ID for each tile.  */
  RangeMap<int32_t> tiles;

  /** Number of non-empty tiles.  */
  size_t numTiles = 0;

  /** Range of region IDs found.  */
  MinMax idRange;

protected:

  void
  ReadTile (const HexCoord& coord, std::istream& in) override
  {
    auto& val = tiles.Access (coord);
    CHECK_EQ (val, -1)
        << "Duplicate tiles in region map for coordinate " << coord;

    val = Read<int32_t> (in);
    idRange.Update (val);

    ++numTiles;
  }

public:

  RegionData ()
    : tiles(HexCoord (), FULL_L1RANGE, -1)
  {}

  RegionData (const RegionData&) = delete;
  void operator= (const RegionData&) = delete;

  const MinMax&
  GetIdRange () const
  {
    return idRange;
  }

  /**
   * Checks that the data matches the expected format.
   */
  void
  CheckData () const
  {
    LOG (INFO) << "Checking region ID data...";

    CHECK_EQ (idRange.minVal, 0) << "Expected region IDs to start at zero";

    std::set<int32_t> regionIds;
    for (int y = GetRanges ().GetRowRange ().minVal;
         y <= GetRanges ().GetRowRange ().maxVal; ++y)
      {
        const auto& colRange = GetRanges ().GetColumnRange (y);
        for (int x = colRange.minVal; x <= colRange.maxVal; ++x)
          {
            const HexCoord c(x, y);
            const auto val = tiles.Get (c);
            CHECK_NE (val, -1) << "No region ID for tile " << c;
            regionIds.emplace (val);
          }
      }

    /* Region IDs are not fully contiguous, since the map has been cropped
       after generation and thus some IDs are missing.  */
    CHECK_LE (regionIds.size (), idRange.maxVal + 1)
        << "Too many region IDs found";

    LOG (INFO) << "We have " << regionIds.size () << " regions";
  }

  /**
   * Writes out data for the region map as generated code and raw binary
   * blobs to the given streams.
   */
  void
  Write (std::ostream& codeOut, std::ostream& mapOut,
         std::ostream& xcoordOut, std::ostream& idsOut) const
  {
    LOG (INFO) << "Writing region map data...";
    codeOut << "namespace regions {" << std::endl;

    int offset = 0;
    codeOut << "const size_t regionIdOffsetForY[] = {" << std::endl;
    for (int y = GetRanges ().GetRowRange ().minVal;
         y <= GetRanges ().GetRowRange ().maxVal; ++y)
      {
        codeOut << "  " << offset << "," << std::endl;

        const auto& colRange = GetRanges ().GetColumnRange (y);
        for (int x = colRange.minVal; x <= colRange.maxVal; ++x)
          {
            const HexCoord c(x, y);
            const auto val = tiles.Get (c);
            CHECK_NE (val, -1) << "No region ID for tile " << c;
            WriteInt24 (mapOut, val);
          }

        offset += (colRange.maxVal - colRange.minVal + 1)
                    * tiledata::regions::BYTES_PER_ID;
      }
    codeOut << "}; // regionIdOffsetForY" << std::endl;
    codeOut << "CHECK_YARRAY_LEN (regionIdOffsetForY);" << std::endl;

    CHECK_EQ (offset, tiledata::regions::BYTES_PER_ID * numTiles);
    codeOut << "const size_t regionMapSize = " << offset << ";" << std::endl;

    int entries = 0;
    codeOut << "const size_t compactOffsetForY[] = {" << std::endl;
    for (int y = GetRanges ().GetRowRange ().minVal;
         y <= GetRanges ().GetRowRange ().maxVal; ++y)
      {
        codeOut << "  " << entries << "," << std::endl;

        using CoordT = int16_t;
        std::vector<CoordT> xCoords;

        const auto& colRange = GetRanges ().GetColumnRange (y);
        int32_t lastVal = -1;
        for (int x = colRange.minVal; x <= colRange.maxVal; ++x)
          {
            const HexCoord c(x, y);
            const auto val = tiles.Get (c);
            CHECK_NE (val, -1) << "No region ID for tile " << c;

            if (val != lastVal)
              {
                xCoords.push_back (x);
                WriteInt24 (idsOut, val);
                ++entries;
                lastVal = val;
              }
          }

        xcoordOut.write (reinterpret_cast<const char*> (xCoords.data ()),
                         sizeof (CoordT) * xCoords.size ());
      }
    codeOut << "}; // compactOffsetForY" << std::endl;
    codeOut << "CHECK_YARRAY_LEN (compactOffsetForY);" << std::endl;

    codeOut << "const size_t compactEntries = " << entries << ";" << std::endl;

    codeOut << "} // namespace regions" << std::endl;
  }

};

} // anonymous namespace
} // namespace pxd

int
main (int argc, char** argv)
{
  google::InitGoogleLogging (argv[0]);

  gflags::SetUsageMessage ("Process raw map data");
  gflags::SetVersionString (PACKAGE_VERSION);
  gflags::ParseCommandLineFlags (&argc, &argv, true);

  CHECK (!FLAGS_obstacle_input.empty ()) << "--obstacle_input must be set";
  CHECK (!FLAGS_region_input.empty ()) << "--region_input must be set";
  CHECK (!FLAGS_code_output.empty ()) << "--code_output must be set";
  CHECK (!FLAGS_obstacle_output.empty ()) << "--obstacle_output must be set";
  CHECK (!FLAGS_region_map_output.empty ())
      << "--region_map_output must be set";
  CHECK (!FLAGS_region_xcoord_output.empty ())
      << "--region_xcoord_output must be set";
  CHECK (!FLAGS_region_ids_output.empty ())
      << "--region_ids_output must be set";

  std::ofstream codeOut(FLAGS_code_output);
  CHECK (codeOut);
  codeOut << "#include \"tiledata.hpp\"" << std::endl;
  codeOut << "namespace pxd {" << std::endl;
  codeOut << "namespace tiledata {" << std::endl;

  pxd::CoordRanges ranges;

  {
    pxd::ObstacleData obstacles;

    std::ifstream in(FLAGS_obstacle_input, std::ios_base::binary);
    CHECK (in) << "Failed to open obstacle input file";

    LOG (INFO) << "Reading obstacle input data...";
    obstacles.ReadInput (in);

    obstacles.GetRanges ().WriteCode (codeOut);

    std::ofstream obstacleOut(FLAGS_obstacle_output, std::ios_base::binary);
    obstacles.Write (codeOut, obstacleOut);

    ranges = obstacles.MoveRanges ();
  }

  {
    pxd::RegionData regions;

    std::ifstream in(FLAGS_region_input, std::ios_base::binary);
    CHECK (in) << "Failed to open region input file";

    LOG (INFO) << "Reading region map input...";
    regions.ReadInput (in);
    LOG (INFO) << "Range of region IDs: " << regions.GetIdRange ();

    CHECK (regions.GetRanges () == ranges) << "Coordinate ranges mismatch";
    regions.CheckData ();

    std::ofstream mapOut(FLAGS_region_map_output, std::ios_base::binary);
    std::ofstream xcoordOut(FLAGS_region_xcoord_output, std::ios_base::binary);
    std::ofstream idsOut(FLAGS_region_ids_output, std::ios_base::binary);
    regions.Write (codeOut, mapOut, xcoordOut, idsOut);
  }

  codeOut << "} // namespace tiledata" << std::endl;
  codeOut << "} // namespace pxd" << std::endl;

  return EXIT_SUCCESS;
}
