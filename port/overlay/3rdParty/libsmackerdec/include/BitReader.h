/*
 * libsmackerdec - Smacker video decoder
 * Copyright (C) 2011 Barry Duncan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* DiabloWarp (Amiga 68060): the whole block (a frame, an audio chunk, the
 * header trees) is read from the file at once into memory, and the bits are
 * taken from there by inline functions. The original read 4 bytes at a time
 * through SDL_RWread (the MPQ layer) and every bit through a call into
 * another file: 97 ms per frame of the Hellfire logo on a 68060.
 * The bit order is the same: least significant bit of each byte first. */

#ifndef _SmackerBitReader_h_
#define _SmackerBitReader_h_

#include <stdint.h>
#include "FileStream.h"

namespace SmackerCommon {

class BitReader
{
	public:
		BitReader(SmackerCommon::FileStream &file, uint32_t size);
		~BitReader();

		inline uint32_t GetBit()
		{
			uint32_t ret = (data[currentOffset >> 3] >> (currentOffset & 7)) & 1;
			currentOffset++;
			return ret;
		}

		/* n = 0..32 */
		inline uint32_t GetBits(uint32_t n)
		{
			if (n > 24) {
				uint32_t lo = GetBits(16);
				return lo | (GetBits(n - 16) << 16);
			}
			const uint8_t *p = data + (currentOffset >> 3);
			uint32_t v = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
			v >>= currentOffset & 7;
			currentOffset += n;
			return v & ((1u << n) - 1);
		}

		/* the next 8 bits without moving (zeros after the end of the block) */
		inline uint32_t PeekBits8()
		{
			const uint8_t *p = data + (currentOffset >> 3);
			return ((p[0] | (p[1] << 8)) >> (currentOffset & 7)) & 0xFF;
		}

		/* the block is already read from the file: only the position moves */
		inline void SkipBits(uint32_t n) { currentOffset += n; }

		uint32_t GetSize() { return totalSize * 8; }
		uint32_t GetPosition() { return currentOffset; }

	private:
		uint32_t totalSize;
		uint32_t currentOffset;
		const uint8_t *data;
};

} // close namespace SmackerCommon

#endif
