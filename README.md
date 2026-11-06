# NetGPU-ICD

An installable client driver (ICD) for sharing GPU processing power over a LAN.

# Notes (can't be arsed writing proper documentation ATM)

Physical AF_PACKET ping/pong is FIXED between the client and host. On the client, `sudo setcap cap_net_raw+ep build/packet_link_smoke` lets you run the smoke tool without `sudo`. Remove the capability with `sudo setcap -r build/packet_link_smoke` when no longer needed. Enables raw packet access, do NOT use this lightly, this should be fixed somehow.