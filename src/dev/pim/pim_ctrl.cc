#include "dev/pim/pim_ctrl.hh"

#include "base/trace.hh"
#include "debug/PIMCtrl.hh"
#include "mem/packet_access.hh"
#include "mem/pim_op.hh"
#include "mem/pimsim.hh"

namespace gem5
{

PIMCtrl::PIMCtrl(const PIMCtrlParams &p)
    : BasicPioDevice(p, PIM_REG_SIZE),
      pimMem(p.pim_mem),
      cmd(0),
      status(0),
      err(ERR_NONE),
      src0(0),
      src1(0),
      dst(0),
      numTile(0),
      m(0),
      n(0),
      k(0),
      doneEvent([this] { finishPIM(); }, name() + ".doneEvent")
{
}

uint64_t
PIMCtrl::readReg(Addr offset) const
{
    switch (offset) {
      case REG_CMD:
        return cmd;
      case REG_STATUS:
        return status;
      case REG_SRC0:
        return src0;
      case REG_SRC1:
        return src1;
      case REG_DST:
        return dst;
      case REG_NUM_TILE:
        return numTile;
      case REG_M:
        return m;
      case REG_N:
        return n;
      case REG_K:
        return k;
      case REG_ERR:
        return err;
      default:
        return 0;
    }
}

void
PIMCtrl::writeReg(Addr offset, uint64_t value)
{
    switch (offset) {
      case REG_CMD:
        cmd = value;
        break;

      case REG_SRC0:
        src0 = value;
        break;

      case REG_SRC1:
        src1 = value;
        break;

      case REG_DST:
        dst = value;
        break;

      case REG_NUM_TILE:
        numTile = value;
        break;

      case REG_M:
        m = static_cast<uint32_t>(value);
        break;

      case REG_N:
        n = static_cast<uint32_t>(value);
        break;

      case REG_K:
        k = static_cast<uint32_t>(value);
        break;

      case REG_START:
        if (value == 1) {
            startPIM();
        }
        break;

      case REG_CLEAR:
        clearStatus();
        break;

      default:
        break;
    }
}

void
PIMCtrl::clearStatus()
{
    if (doneEvent.scheduled()) {
        deschedule(doneEvent);
    }

    status = 0;
    err = ERR_NONE;
}

void
PIMCtrl::startPIM()
{
    if (status & STATUS_BUSY) {
        status |= STATUS_ERROR;
        err = ERR_BUSY;
        return;
    }

    if (pimMem == nullptr) {
        status |= STATUS_ERROR;
        err = ERR_NO_PIM_MEM;
        return;
    }

    memory::PIMOp op;

    switch (cmd) {
      case 1:
        op.opcode = memory::PIMOpcode::Gemv;
        break;
      case 2:
        op.opcode = memory::PIMOpcode::Add;
        break;
      case 3:
        op.opcode = memory::PIMOpcode::Mul;
        break;
      case 4:
        op.opcode = memory::PIMOpcode::Relu;
        break;
      default:
        status |= STATUS_ERROR;
        err = ERR_BAD_CMD;
        return;
    }

    op.src0 = src0;
    op.src1 = src1;
    op.dst = dst;
    op.numTile = numTile;
    op.m = m;
    op.n = n;
    op.k = k;

    if (numTile == 0 && op.opcode != memory::PIMOpcode::Gemv) {
        status |= STATUS_ERROR;
        err = ERR_BAD_PARAM;
        return;
    }

    status &= ~STATUS_DONE;
    status &= ~STATUS_ERROR;
    status |= STATUS_BUSY;
    err = ERR_NONE;

    /*
     * 关键调用：通过 PIMSim 进入 PIMSimulator。
     */
    Tick latency = pimMem->submitPIMOp(op);

    if (latency == 0) {
        latency = pioDelay;
    }

    schedule(doneEvent, curTick() + latency);

    DPRINTF(PIMCtrl,
            "Start PIM op cmd=%lu src0=%lu src1=%lu dst=%lu "
            "numTile=%lu m=%u n=%u k=%u latency=%lu\n",
            cmd, src0, src1, dst, numTile, m, n, k, latency);
}

void
PIMCtrl::finishPIM()
{
    status &= ~STATUS_BUSY;
    status |= STATUS_DONE;

    DPRINTF(PIMCtrl, "PIM op finished\n");
}

// Tick
// PIMCtrl::read(PacketPtr pkt)
// {
//     const Addr offset = pkt->getAddr() - pioAddr;
//     const uint64_t value = readReg(offset);

//     if (pkt->getSize() == 8) {
//         pkt->setLE<uint64_t>(value);
//     } else if (pkt->getSize() == 4) {
//         pkt->setLE<uint32_t>(static_cast<uint32_t>(value));
//     } else {
//         pkt->setLE<uint8_t>(static_cast<uint8_t>(value));
//     }

//     pkt->makeAtomicResponse();
//     return pioDelay;
// }

Tick
PIMCtrl::read(PacketPtr pkt)
{
    const Addr offset = pkt->getAddr() - pioAddr;
    const uint64_t value = readReg(offset);

    switch (pkt->getSize()) {
      case 8:
        pkt->setLE<uint64_t>(value);
        break;

      case 4:
        pkt->setLE<uint32_t>(static_cast<uint32_t>(value));
        break;

      case 2:
        pkt->setLE<uint16_t>(static_cast<uint16_t>(value));
        break;

      case 1:
        pkt->setLE<uint8_t>(static_cast<uint8_t>(value));
        break;

      default:
        panic("PIMCtrl read with unsupported size %u\n", pkt->getSize());
    }

    pkt->makeAtomicResponse();
    return pioDelay;
}

// Tick
// PIMCtrl::write(PacketPtr pkt)
// {
//     const Addr offset = pkt->getAddr() - pioAddr;

//     uint64_t value = 0;

//     if (pkt->getSize() == 8) {
//         value = pkt->getLE<uint64_t>();
//     } else if (pkt->getSize() == 4) {
//         value = pkt->getLE<uint32_t>();
//     } else {
//         value = pkt->getLE<uint8_t>();
//     }

//     writeReg(offset, value);

//     pkt->makeAtomicResponse();
//     return pioDelay;
// }

Tick
PIMCtrl::write(PacketPtr pkt)
{
    const Addr offset = pkt->getAddr() - pioAddr;

    uint64_t value = 0;

    switch (pkt->getSize()) {
      case 8:
        value = pkt->getLE<uint64_t>();
        break;

      case 4:
        value = pkt->getLE<uint32_t>();
        break;

      case 2:
        value = pkt->getLE<uint16_t>();
        break;

      case 1:
        value = pkt->getLE<uint8_t>();
        break;

      default:
        panic("PIMCtrl write with unsupported size %u\n", pkt->getSize());
    }

    writeReg(offset, value);

    pkt->makeAtomicResponse();
    return pioDelay;
}

} // namespace gem5/ namespace gem5