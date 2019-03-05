#include "regionmap.hpp"

#include "tiledata.hpp"

#include <glog/logging.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

namespace pxd
{

namespace
{

/**
 * Implementation of RegionMap that assumes we have some big array of all
 * the region IDs and use it to look up the ID for each coordinate.
 */
class ArrayRegionMap : public RegionMap
{

protected:

  /**
   * Returns the raw data encoding the region ID at the given offset.  This
   * function just accesses the raw, static data file.  It can be implemented
   * using different techniques, e.g. direct file seeking, loading all data
   * into memory, or memory-mapping the data file.
   */
  virtual const unsigned char* AccessData (const size_t offs) const = 0;

public:

  IdT
  GetRegionForTile (const HexCoord& c) const override
  {
    const auto x = c.GetX ();
    const auto y = c.GetY ();

    CHECK_GE (y, tiledata::minY);
    CHECK_LE (y, tiledata::maxY);
    const int yInd = y - tiledata::minY;

    CHECK_GE (x, tiledata::minX[yInd]);
    CHECK_LE (x, tiledata::maxX[yInd]);
    const int xInd = x - tiledata::minX[yInd];

    const size_t offs
        = tiledata::regions::regionIdOffsetForY[yInd]
            + tiledata::regions::BYTES_PER_ID * xInd;
    const unsigned char* data = AccessData (offs);

    IdT res = 0;
    for (int i = 0; i < tiledata::regions::BYTES_PER_ID; ++i)
      res |= (static_cast<IdT> (data[i]) << (8 * i));

    return res;
  }

};

/**
 * Implementation of RegionMap that just loads the datafile into memory
 * once and then accesses the data from there directly.
 */
class InMemoryRegionMap : public ArrayRegionMap
{

private:

  /** The underlying data buffer.  */
  std::vector<unsigned char> data;

protected:

  const unsigned char*
  AccessData (const size_t offs) const
  {
    return data.data () + offs;
  }

public:

  /**
   * Constructs an instance with the data loaded from the given file.
   */
  InMemoryRegionMap (const std::string& filename)
  {
    data.resize (tiledata::regions::regionMapSize);
    LOG (INFO)
        << "Loading " << data.size ()
        << " bytes for the region map from " << filename
        << " into memory...";

    std::ifstream in(filename, std::ios_base::binary);
    CHECK (in);
    in.read (reinterpret_cast<char*> (data.data ()), data.size ());
    CHECK_EQ (in.gcount (), data.size ());

    LOG (INFO) << "Finished loading region map data";
  }

};

/**
 * Implementation of RegionMap that holds an underlying file stream and
 * seeks in it to read data.
 */
class StreamRegionMap : public ArrayRegionMap
{

private:

  /**
   * The underlying file.  Using FILE instead of std::ifstream makes the
   * code at least a bit faster.
   */
  mutable FILE* f;

  /** Buffer holding the last read data bytes.  */
  mutable unsigned char buf[tiledata::regions::BYTES_PER_ID];

protected:

  const unsigned char*
  AccessData (const size_t offs) const
  {
    CHECK_EQ (std::fseek (f, offs, SEEK_SET), 0);
    CHECK_EQ (std::fread (buf, 1, tiledata::regions::BYTES_PER_ID, f),
              tiledata::regions::BYTES_PER_ID);
    return buf;
  }

public:

  /**
   * Constructs an instance based on the given data file.
   */
  StreamRegionMap (const std::string& filename)
  {
    LOG (INFO) << "Opening " << filename << " into streamed region map";

    f = std::fopen (filename.c_str (), "rb");
    CHECK (f != nullptr);
  }

  ~StreamRegionMap ()
  {
    std::fclose (f);
  }

};

/**
 * Implementation of RegionMap that memory-maps the static data file
 * for reading from it.
 */
class MemMappedRegionMap : public ArrayRegionMap
{

private:

  /** The underlying file descriptor.  */
  int fd;

  /** Pointer to the mapped memory.  */
  unsigned char* data;

protected:

  const unsigned char*
  AccessData (const size_t offs) const
  {
    return data + offs;
  }

public:

  /**
   * Constructs an instance based on the given data file.
   */
  MemMappedRegionMap (const std::string& filename)
  {
    LOG (INFO) << "Memory-mapping " << filename << " for region map";
    fd = open (filename.c_str (), O_RDONLY);
    CHECK_NE (fd, -1) << "Failed to open " << filename << ": " << errno;

    struct stat sb;
    CHECK_EQ (fstat (fd, &sb), 0) << "stat failed with code " << errno;
    CHECK_EQ (sb.st_size, tiledata::regions::regionMapSize);

    auto* ptr = mmap (nullptr, tiledata::regions::regionMapSize,
                      PROT_READ, MAP_PRIVATE, fd, 0);
    CHECK_NE (ptr, MAP_FAILED) << "mmap failed with code " << errno;
    data = static_cast<unsigned char*> (ptr);
  }

  ~MemMappedRegionMap ()
  {
    CHECK_EQ (munmap (data, tiledata::regions::regionMapSize), 0)
        << "munmap failed with code " << errno;
    close (fd);
  }

};

/**
 * Region-map implementation that uses the embedded, compacted data
 * for the lookups.
 */
class CompactRegionMap : public RegionMap
{

public:

  CompactRegionMap ()
  {
    using tiledata::regions::compactEntries;
    CHECK_EQ (&blob_region_xcoord_end - &blob_region_xcoord_start,
              compactEntries);
    CHECK_EQ (&blob_region_ids_end - &blob_region_ids_start,
              tiledata::regions::BYTES_PER_ID * compactEntries);
  }

  IdT
  GetRegionForTile (const HexCoord& c) const override
  {
    const auto x = c.GetX ();
    const auto y = c.GetY ();

    CHECK_GE (y, tiledata::minY);
    CHECK_LE (y, tiledata::maxY);
    const int yInd = y - tiledata::minY;

    CHECK_GE (x, tiledata::minX[yInd]);
    CHECK_LE (x, tiledata::maxX[yInd]);

    using tiledata::regions::compactOffsetForY;
    const int16_t* xBegin = &blob_region_xcoord_start + compactOffsetForY[yInd];
    const int16_t* xEnd;
    if (y < tiledata::maxY)
      xEnd = &blob_region_xcoord_start + compactOffsetForY[yInd + 1];
    else
      xEnd = &blob_region_xcoord_end;

    /* Calling std::upper_bound on the sorted row of x coordinates gives us
       the first element that is larger than our x.  This means that the
       entry we are looking for is the one just before it, as the largest
       that is less-or-equal to x.  Note that *xBegin is guaranteed to be the
       minimum x value, so that we are guaranteed to not get xBegin itself.  */
    const auto* xFound = std::upper_bound (xBegin, xEnd, x);
    CHECK_EQ (*xBegin, tiledata::minX[yInd]);
    CHECK_GT (xFound, xBegin);
    --xFound;
    CHECK_LE (*xFound, x);

    using tiledata::regions::BYTES_PER_ID;
    const size_t offs = xFound - &blob_region_xcoord_start;
    const unsigned char* data = &blob_region_ids_start + BYTES_PER_ID * offs;

    IdT res = 0;
    for (int i = 0; i < BYTES_PER_ID; ++i)
      res |= (static_cast<IdT> (data[i]) << (8 * i));

    return res;
  }

};

} // anonymous namespace

std::unique_ptr<RegionMap>
NewInMemoryRegionMap (const std::string& filename)
{
  return std::make_unique<InMemoryRegionMap> (filename);
}

std::unique_ptr<RegionMap>
NewStreamRegionMap (const std::string& filename)
{
  return std::make_unique<StreamRegionMap> (filename);
}

std::unique_ptr<RegionMap>
NewMemMappedRegionMap (const std::string& filename)
{
  return std::make_unique<MemMappedRegionMap> (filename);
}

std::unique_ptr<RegionMap>
NewCompactRegionMap ()
{
  return std::make_unique<CompactRegionMap> ();
}

} // namespace pxd
