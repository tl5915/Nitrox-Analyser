# Nitrox-Analyser
Nitrox Analyser (Divesoft DNA clone)

Material required:
50mL Syringe;
  (cut the tip off and drill to 16mm, trim to approriate length, around 63mm)
  (keep the silicone plug on the plunger, cut holes for usb-c as end plug)
M16x1 Nut (can be 3D printed)
Flow Limiter (can be 3D printed)
Oxygen Cell (any 3-Pin Molex type, e.g. Vandagraph R-22VAN)
Seeed Xiao ESP32-C3
LM358P Operational Amplifier
3mm 20mA Green LED
100/3.3k/47k ohm Resistors
0.1uF Ceramic Capacitors
Prototype Board (cut to 7x8)
8-Pin Female Headers
JST-XH (2.54mm) 3-Pin Female Connector 
  (cut the locking arrows off to fit Molex header on oxygen cell)

Circuit connection:
LM358P pin 8 (Vcc) connects to Xiao 3V3 pin
LM358P pin 4 (Gnd) connects to Xiao Gnd pin and Oxygen Cell pin 1 (-)
LM358P pin 3 (IN1+) connects to Oxygen Cell pin 3 (+)
LM358P pin 1 (OUT1) connects to Xiao pin A2
3.3k ohm resistor connects between LM358P pin 2 (IN1-) and pin 4 (Gnd)
47k ohm resistor connects between LM358P pin 1 (OUT1) and pin 2 (IN2-)
0.1uF capacitor connects between LM358P pin 8 and 3
0.1uF capacitor connects between Xiao pin A2 and Gnd pin
100 ohm resistor connects between Xiao pin D7 and LED long leg (+)
LED short leg (-) connects to Xiao pin D6
