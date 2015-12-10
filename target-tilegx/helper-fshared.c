/*
 *  TILE-Gx virtual Floating point shared functions
 *
 *  Copyright (c) 2015 Chen Gang
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

static inline uint64_t create_fsfd_flag_un(void)
{
    return 1 << 25;
}

static inline uint64_t create_fsfd_flag_lt(void)
{
    return 1 << 26;
}

static inline uint64_t create_fsfd_flag_le(void)
{
    return 1 << 27;
}

static inline uint64_t create_fsfd_flag_gt(void)
{
    return 1 << 28;
}

static inline uint64_t create_fsfd_flag_ge(void)
{
    return 1 << 29;
}

static inline uint64_t create_fsfd_flag_eq(void)
{
    return 1 << 30;
}

static inline uint64_t create_fsfd_flag_ne(void)
{
    return 1ULL << 31;
}
