/* Utility program to read the raw map data files and convert them into a
   format that is compact and easy to read at runtime.  The processed data
   is then used by the BaseMap class.  */

#include "config.h"

#include "dataio.hpp"

#include "hexagonal/coord.hpp"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

DEFINE_string (obstacle_input, "",
               "The file with input obstacle data");
DEFINE_string (code_output, "",
               "Write processed metadata as C++ code to this file");
DEFINE_string (obstacle_output, "",
               "Write raw binary data for the obstacle layer to this file");

namespace pxd
{
namespace
{

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

  /** Coordinate ranges seen.  */
  CoordRanges ranges;

protected:

  PerTileData () = default;

  PerTileData (const PerTileData&) = delete;
  void operator= (const PerTileData&) = delete;

  /**
   * Resets all stored per-tile data.
   */
  virtual void Clear () = 0;

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
    ranges.Clear ();
    Clear ();

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

  /** Map of already read obstacle "tiles" from the raw input.  */
  std::unordered_map<HexCoord, bool> passableMap;

protected:

  void
  Clear () override
  {
    passableMap.clear ();
  }

  void
  ReadTile (const HexCoord& coord, std::istream& in) override
  {
    const bool passable = Read<int16_t> (in);
    const auto res = passableMap.emplace (coord, passable);
    CHECK (res.second) << "Duplicate tiles in obstacle data input";
  }

public:

  ObstacleData () = default;

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
            const auto mitPassable = passableMap.find (c);
            CHECK (mitPassable != passableMap.end ())
                << "No passable data for tile " << c;
            bits.Append (mitPassable->second);
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
  CHECK (!FLAGS_code_output.empty ()) << "--code_output must be set";
  CHECK (!FLAGS_obstacle_output.empty ()) << "--obstacle_output must be set";

  pxd::ObstacleData obstacles;
  {
    std::ifstream in(FLAGS_obstacle_input, std::ios_base::binary);
    CHECK (in) << "Failed to open obstacle input file";

    LOG (INFO) << "Reading obstacle input data...";
    obstacles.ReadInput (in);
  }

  std::ofstream out(FLAGS_code_output);
  std::ofstream obstacleOut(FLAGS_obstacle_output, std::ios_base::binary);
  out << "#include \"tiledata.hpp\"" << std::endl;
  out << "namespace pxd {" << std::endl;
  out << "namespace tiledata {" << std::endl;
  obstacles.GetRanges ().WriteCode (out);
  obstacles.Write (out, obstacleOut);
  out << "} // namespace tiledata" << std::endl;
  out << "} // namespace pxd" << std::endl;

  return EXIT_SUCCESS;
}
