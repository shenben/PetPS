#!/bin/bash
# Script to enable large MTU for ConnectX-6 RNIC
# For dual-port cards, both IB and RoCE ports should use the same MTU
#
# Note: ConnectX-6 dual-port cards have:
#   - One port can be IB mode (limited to ~2044 MTU)
#   - One port can be RoCE/Ethernet mode (supports up to 4200 MTU)
#
# For large MTU (4200), use RoCE mode on the Ethernet port

set -e

# Load MST PCI modules for Mellanox NICs (for firmware/configuration access)
echo "Loading MST PCI modules..."
sudo modprobe mst_pci
sudo modprobe mst_pciconf
echo "MST modules loaded successfully."

# Check MST status
echo ""
echo "MST Devices:"
sudo mst status 2>/dev/null || echo "  (mst status unavailable)"

# Find all IB and Ethernet interfaces for the NIC
echo ""
echo "Detecting ConnectX-6 interfaces..."

# Get all IB interfaces
IB_INTERFACES=$(ls /sys/class/net/ 2>/dev/null | grep -E "^ibp" || true)

# Get all Ethernet interfaces that might be RoCE ports
# Look for enp* interfaces that correspond to the same PCI device as IB
ROCE_INTERFACES=$(ls /sys/class/net/ 2>/dev/null | grep -E "^enp.*202s0|^enp.*ca:00" || true)

echo "IB interfaces found: ${IB_INTERFACES:-none}"
echo "RoCE interfaces found: ${ROCE_INTERFACES:-none}"

# For ConnectX-6 with RoCE, set MTU to 4200 on Ethernet/RoCE interfaces
echo ""
echo "Setting large MTU (4200) for RoCE/Ethernet interfaces..."

for iface in $ROCE_INTERFACES; do
    if [ -d "/sys/class/net/$iface" ]; then
        CURRENT_MTU=$(cat /sys/class/net/$iface/mtu 2>/dev/null || echo "unknown")
        echo "  Setting $iface (current MTU: $CURRENT_MTU) to 4200..."
        sudo ip link set $iface mtu 4200
        NEW_MTU=$(cat /sys/class/net/$iface/mtu)
        echo "  $iface MTU is now: $NEW_MTU"

        if [ "$NEW_MTU" != "4200" ]; then
            echo "  WARNING: Failed to set MTU to 4200!"
        fi
    fi
done

# Check if IB interfaces need different MTU (IB is typically limited to 2044)
echo ""
echo "IB interfaces (limited to 2044 in IB mode):"
for iface in $IB_INTERFACES; do
    if [ -d "/sys/class/net/$iface" ]; then
        MTU=$(cat /sys/class/net/$iface/mtu 2>/dev/null || echo "N/A")
        echo "  $iface: MTU $MTU (IB mode - max ~2044)"
    fi
done

echo ""
echo "=== Configuration Summary ==="
echo "RoCE/Ethernet MTU: 4200 (enabled for large packet transfers)"
echo "IB MTU: 2044 (standard InfiniBand limit)"
echo ""
echo "For best performance with PetPS using RoCE:"
echo "  1. Use the RoCE interface (enp202s0f1np1) with MTU 4200"
echo "  2. The application code uses IBV_MTU_4096 in StateTrans.cpp"
echo "  3. Network path MTU should match or exceed 4096"
echo ""
echo "Done!"

# Also update the code if needed (optional - based on README)
echo ""
echo "Note: If your NIC shows different MTU values, you may need to update"
echo "      third_party/Mayfly-main/src/rdma/StateTrans.cpp to match."
