/* Utility program to read the raw map data files and convert them into a
   format that is compact and easy to read at runtime.  The processed data
   is then used by the BaseMap class.  */

#include "config.h"

#include "dataio.hpp"

#include "hexagonal/coord.hpp"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

DEFINE_string (obstacle_input, "",
               "The file with input obstacle data");
DEFINE_string (binary_output, "",
               "If not empty, write processed binary data to this file");
DEFINE_string (code_output, "",
               "If not empty, write processed data as C++ code to this file");

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
 * Holds the obstacle data for the base map.  Since the map is "square",
 * this corresponds to a number of "rows" (axial y coordinate), where
 * each row has a variable length between some bounds for the "column"
 * (axial x coordinate).
 */
class ObstacleData
{

private:

  /** Map of already read obstacle "tiles" from the raw input.  */
  std::unordered_map<HexCoord, bool> passableMap;

  /** The minimum and maximum seen row values.  */
  MinMax rowRange;

  /** For each row, the range of seen column values.  */
  std::unordered_map<int, MinMax> columnRange;

public:

  ObstacleData () = default;

  ObstacleData (const ObstacleData&) = delete;
  void operator= (const ObstacleData&) = delete;

  /**
   * Reads in the data from the input obstacle binary stream.  The format
   * of that data file is as follows (all little-endian 16-bit signed integers):
   *
   * 2 ints giving the rows/columns of the square map (N * M),
   * (N * M) triplets of ints follow, giving axial x, axial y and passable
   * as 0 or 1.
   */
  void
  ReadInput (std::istream& in)
  {
    LOG (INFO) << "Reading obstacle input binary data...";
    passableMap.clear ();
    columnRange.clear ();
    rowRange = MinMax ();

    const size_t n = ReadInt16 (in);
    const size_t m = ReadInt16 (in);
    LOG (INFO)
        << "Reading " << n << " * " << m << " = " << (n * m) << " tiles";
    for (size_t i = 0; i < n * m; ++i)
      {
        const int16_t x = ReadInt16 (in);
        const int16_t y = ReadInt16 (in);
        const bool passable = ReadInt16 (in);
        passableMap.emplace (HexCoord (x, y), passable);
        rowRange.Update (y);
        columnRange[y].Update (x);
      }

    CHECK (passableMap.size () == n * m)
        << "Duplicate map tiles in obstacle data";

    LOG (INFO) << "Finished reading obstacle input data";
    LOG (INFO) << "Row range: " << rowRange.minVal << " ... " << rowRange.maxVal;
  }

  /**
   * Writes the data as compact rows of bit vectors.  The format of this
   * data is as follows (signed little-endian 16-bit integers and raw bytes
   * for the bit vector):
   *
   * MIN-ROW MAX-ROW
   * For each row between those (inclusive);
   *   MIN-COL MAX-COL
   *   Passable for the columns between those (inclusive) encoded as a bit
   *   vector with 8 passable-bits per byte and in "little endian", i.e.
   *   the least-significant bit in each byte corresponds to the lowest-valued
   *   column tile.
   *
   * As before, here "row" is the axial y coordinate and "column" the axial
   * x coordinate.
   */
  void
  WriteCompact (std::ostream& out) const
  {
    LOG (INFO) << "Writing obstacle data compactly...";
    WriteInt16 (out, rowRange.minVal);
    WriteInt16 (out, rowRange.maxVal);
    for (int y = rowRange.minVal; y <= rowRange.maxVal; ++y)
      {
        const auto mit = columnRange.find (y);
        CHECK (mit != columnRange.end ()) << "No column data for row " << y;
        WriteInt16 (out, mit->second.minVal);
        WriteInt16 (out, mit->second.maxVal);

        BitVectorBuilder bits;
        for (int x = mit->second.minVal; x <= mit->second.maxVal; ++x)
          {
            const HexCoord c(x, y);
            const auto mitPassable = passableMap.find (c);
            CHECK (mitPassable != passableMap.end ())
                << "No passable data for tile " << c;
            bits.Append (mitPassable->second);
          }

        bits.Finalise ();
        const auto& data = bits.GetData ();
        out.write (reinterpret_cast<const char*> (data.data ()), data.size ());
      }
  }

  /**
   * Writes out C++ code that encodes the data into some constants, so that
   * it can be compiled directly into the binary.  The raw rows are still
   * encoded as bit vectors to save memory.
   */
  void
  WriteCode (std::ostream& out) const
  {
    LOG (INFO) << "Writing obstacle data as C++ code constants...";

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

    /* We store all the bit-vector data into one big array of bytes that is
       encoded in a single array in code.  We also store the index at which
       each row's data starts in that array into another constant array.  */
    std::vector<unsigned char> bitData;
    out << "const int bitDataOffsetForY[] = {" << std::endl;
    for (int y = rowRange.minVal; y <= rowRange.maxVal; ++y)
      {
        out << "  " << bitData.size () << "," << std::endl;
        const auto& colRange = columnRange.at (y);

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
        bitData.insert (bitData.end (), data.begin (), data.end ());
      }
    out << "}; // bitDataOffsetForY" << std::endl;

    out << R"(
      static_assert (sizeof (minX) == (maxY - minY + 1) * sizeof (minX[0]),
                     "minX has unexpected size");
      static_assert (sizeof (maxX) == sizeof (minX),
                     "maxX has unexpected size");
      static_assert (sizeof (bitDataOffsetForY) == sizeof (minX),
                     "bitDataOffsetForY has unexpected size");
    )";

    out << "const unsigned char bitData[] = {" << std::endl;
    for (const int byte : bitData)
      out << "  " << byte << "," << std::endl;
    out << "}; // bitData" << std::endl;
    out << "static_assert (sizeof (bitData) == " << bitData.size ()
        << ", \"bitData has unexpected size\");" << std::endl;
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

  pxd::ObstacleData obstacles;
  {
    std::ifstream in(FLAGS_obstacle_input, std::ios_base::binary);
    CHECK (in) << "Failed to open obstacle input file";
    obstacles.ReadInput (in);
  }

  if (!FLAGS_binary_output.empty ())
    {
      std::ofstream out(FLAGS_binary_output, std::ios_base::binary);
      obstacles.WriteCompact (out);
    }

  if (!FLAGS_code_output.empty ())
    {
      std::ofstream out(FLAGS_code_output);
      out << "#include \"obstacles.hpp\"" << std::endl;
      out << "namespace pxd {" << std::endl;
      out << "namespace obstacles {" << std::endl;
      obstacles.WriteCode (out);
      out << "} // namespace obstacles" << std::endl;
      out << "} // namespace pxd" << std::endl;
    }

  return EXIT_SUCCESS;
}
