#ifndef PXD_AMOUNT_HPP
#define PXD_AMOUNT_HPP

#include <cstdint>

namespace pxd
{

/** An amount of CHI, represented as Satoshi.  */
using Amount = int64_t;

/** Amount of one CHI.  */
constexpr Amount COIN = 100000000;

/**
 * Highest valid value for an amount.  This is just used for sanity checks
 * and not the precise money supply of CHI.
 */
constexpr Amount MAX_AMOUNT = 80000000 * COIN;

} // namespace pxd

#endif // PXD_AMOUNT_HPP
