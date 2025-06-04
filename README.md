# IBIS_Sender
IBIS Display Controller via MQTT

This Arduino sketch connects to a Wi-Fi network and subscribes to MQTT topics to control
a IBIS FlipDot display and builtin lighting. It is intended for use with Home Assistant or
other MQTT-based automation systems.

Features:
 - Connects to Wi-Fi and an MQTT broker
 - Listens for display messages on the topic: "home/flipdot/message"
 - Listens for lighting control on the topic: "home/flipdot/lighting" (expects "On"/"Off")
 - Constructs and sends telegrams following the IBIS protocol used by VDV displays
 - Displays the message on a IBIS FlipDot screen via UART using 7E2 serial configuration and a logic level shifter (IBIS Wandler)

 Special thanks to Cato and Matti for sharing their codebases. These helped a lot while reverse engineering the IBIS protocol:
 CatoLynx - pyFIS: https://github.com/CatoLynx/pyFIS
 drive-n-code - Open ITCS: https://github.com/open-itcs/onboard-panel-arduino

Use a "IBIS Wandler" to communicate with the FlipDot display (Converts TTL signals to 24V IBIS logic level).
You can find schematics for uni- and bidirectional communication in this repository where only the minimalistic unidirectional version
is necessary for displaying information on the display.
