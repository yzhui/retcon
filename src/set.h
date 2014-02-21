//  retcon
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_SET
#define HGUARD_SRC_SET

#include "univdefs.h"
#include <memory>
#include <functional>

#ifdef RETCON_STD_STL
#include <set>
#else

#include <wx/version.h>
#include <wx/defs.h>

#if wxCHECK_GCC_VERSION(4, 6)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#pragma GCC diagnostic ignored "-Wshadow"
#endif

#include "cpp-btree/btree_set.h"

#if wxCHECK_GCC_VERSION(4, 6)
#pragma GCC diagnostic pop
#endif

#endif

namespace container {

template<
	class Key,
	class Compare = std::less<Key>,
	class Allocator = std::allocator<Key>
> using set =

#ifdef RETCON_STD_STL
std::set<Key, Compare, Allocator>;
#else
btree::btree_set<Key, Compare, Allocator>;
#endif

};

#endif
