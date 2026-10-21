#ifndef __MEM_RELIABLE_PIM_PIM_ALERT_HH__
#define __MEM_RELIABLE_PIM_PIM_ALERT_HH__
namespace gem5
{
namespace reliable_pim
{
// Logical sticky active-low alarm, not a pin timing/electrical model.
class PimAlertLatch
{
  public:
    bool levelHigh() const { return !asserted; }
    bool pending() const { return asserted; }

    // Return true only for a high-to-low transition.
    bool observe(bool errorDetected)
    {
        if (!errorDetected || asserted)
            return false;
        asserted = true;
        return true;
    }

    // Only a future successful recovery/writeback path may call this.
    void releaseAfterRecovery() { asserted = false; }

  private:
    bool asserted = false;
};
} // namespace reliable_pim
} // namespace gem5
#endif
