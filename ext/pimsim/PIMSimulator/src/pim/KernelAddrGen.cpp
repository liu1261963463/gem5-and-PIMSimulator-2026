/***************************************************************************************************
 * Copyright (C) 2021 Samsung Electronics Co. LTD
 *
 * This software is a property of Samsung Electronics.
 * No part of this software, either material or conceptual may be copied or distributed,
 * transmitted, transcribed, stored in a retrieval system, or translated into any human
 * or computer language in any form by any means,electronic, mechanical, manual or otherwise,
 * or disclosed to third parties without the express written permission of Samsung Electronics.
 * (Use of the Software is restricted to non-commercial, personal or academic, research purpose
 * only)
 **************************************************************************************************/

#include <iomanip>

#include <cstdint>
#include <sstream>
#include <stdexcept>

#include "AddressMapping.h"
#include "pim/PIMKernel.h"

unsigned PIMAddrManager::maskByBit(unsigned value, int start, int end)
{
    int length = start - end + 1;
    value = value >> end;
    return value & ((1 << length) - 1);
}

uint64_t PIMAddrManager::addrGen(unsigned chan, unsigned rank, unsigned bankgroup, unsigned bank,
                                 unsigned row, unsigned col)
{
    uint64_t addr = 0;
    if (address_mapping_scheme_ == Scheme8)
    {
        addr = rank;

        addr <<= num_row_bits_;
        addr |= row;

        addr <<= num_col_bits_;
        addr |= col;

        addr <<= num_bankgroup_bits_;
        addr |= bankgroup;

        addr <<= num_bank_bits_;
        addr |= bank;

        addr <<= num_chan_bits_;
        addr |= chan;

        addr <<= num_offset_bits_;
    }
    else
    {
        // cerr << "Fatal: Not supported address scheme for PIM controller" << endl;
        throw std::runtime_error(
    "PIMAddrManager::addrGen: unsupported address mapping scheme");
    }
    return addr;
}

// uint64_t PIMAddrManager::addrGenSafe(unsigned chan, unsigned rank, unsigned bankgroup,
//                                      unsigned bank, unsigned& row, unsigned& col)
// {
//     while (col >= num_cols_per_bl_)
//     {
//         row++;
//         col -= (num_cols_per_bl_);
//     }

//     if (row >= num_rows_)
//     {
//         cerr << "row overflow" << endl;
//     }
//     return addrGen(chan, rank, bankgroup, bank, row, col);
// }
uint64_t
PIMAddrManager::addrGenSafe(
    unsigned chan,
    unsigned rank,
    unsigned bankgroup,
    unsigned bank,
    unsigned& row,
    unsigned& col)
{
    if (num_cols_per_bl_ == 0 || num_rows_ == 0)
    {
        throw std::runtime_error(
            "PIMAddrManager::addrGenSafe: invalid geometry");
    }

    const uint64_t colsPerRow = num_cols_per_bl_;

    const uint64_t mappedRow =
        static_cast<uint64_t>(row) +
        static_cast<uint64_t>(col) / colsPerRow;

    const uint64_t mappedCol =
        static_cast<uint64_t>(col) % colsPerRow;

    if (mappedRow >= static_cast<uint64_t>(num_rows_))
    {
        std::ostringstream oss;

        oss << "PIMAddrManager::addrGenSafe: row overflow"
            << " chan=" << chan
            << " rank=" << rank
            << " bankgroup=" << bankgroup
            << " bank=" << bank
            << " row_in=" << row
            << " col_in=" << col
            << " mapped_row=" << mappedRow
            << " num_rows=" << num_rows_;

        throw std::out_of_range(oss.str());
    }

    row = static_cast<unsigned>(mappedRow);
    col = static_cast<unsigned>(mappedCol);

    return addrGen(
        chan, rank, bankgroup, bank, row, col);
}
unsigned
PIMAddrManager::flatBankId(
    unsigned bankgroup,
    unsigned bank) const
{
    // 当前ReliablePIM使用bitset<32>。
    if (num_bank_bits_ > 5 ||
        num_bankgroup_bits_ > 5)
    {
        throw std::out_of_range(
            "ReliablePIM: unsupported bank field width");
    }

    const uint64_t banksPerGroup =
        uint64_t{1} << num_bank_bits_;

    const uint64_t bankGroups =
        uint64_t{1} << num_bankgroup_bits_;

    if (bank >= banksPerGroup ||
        bankgroup >= bankGroups)
    {
        throw std::out_of_range(
            "ReliablePIM: invalid bankgroup or bank");
    }

    if (banksPerGroup * bankGroups > 32)
    {
        throw std::out_of_range(
            "ReliablePIM: bank geometry exceeds bitset<32>");
    }

    return static_cast<unsigned>(
        bankgroup * banksPerGroup + bank);
}