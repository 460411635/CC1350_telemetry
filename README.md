# CC1350_telemetry
Use TI CC1350 as drone telemetry. 

## Procedure
1. Unplug Tx/Rx jumpers from one CC1350 Launch Pad and connect it to the flight controller TELEM port
2. Connect a second CC1350 Launch Pad to GCS host UART peripheral (don't unplug the jumper)

CC1350 will copy messages received from UART into the payload of an EasyLink packet and broadcast into the air. 

## 6/7/2019 update: 
No packet loss in Mission Planner

## Bug 
The CC1350 connected to GCS will stop working after ~10 minutes. Restart and it will reconnect. This bug does not occur in the CC1350 on flight. 
