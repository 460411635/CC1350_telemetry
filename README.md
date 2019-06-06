# CC1350_telemetry
Use TI CC1350 as drone telemetry. 

## Procedure
1. Unplug Tx/Rx jumpers from CC1350 Launch Pad
2. Configure the destination address (default: 0xff)
3. Connect DIO 2 (RX) and DIO3 (TX) to your UART ports

CC1350 will copy messages received from UART into the payload of an EasyLink packet and broadcast into the air. 
