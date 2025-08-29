## Assembly

Mounting onto DIN rail -- click the Railduino Module onto the DIN rail (TS 35) as in the picture.

<figure markdown="span">
![](media/dinrail.png){style="max-height:150px;"}
</figure>

## DIP switch settings 

!!! info "Pins 1-4 - Physical Address"
	- the 4-bit physical address (pins 1–4) allows for 16 unique addresses (0 to 15).  
    - ensure each device on the network has a unique address to avoid conflicts.  
	- number 1 in table below means the switch is in ON position
	<table>
	  <th>Pins 1234</th><th>Address</th><th>Pins 1234</th><th>Address</th><th>Pins 1234</th><th>Address</th><th>Pins 1234</th><th>Address</th>
	  <tr><td>0000</td><td>0</td><td>0100</td><td>4</td><td>1000</td><td>8</td><td>1100</td><td>12</td></tr>
	  <tr><td>0001</td><td>1</td><td>0101</td><td>5</td><td>1001</td><td>9</td><td>1101</td><td>13</td></tr>
	  <tr><td>0010</td><td>2</td><td>0110</td><td>6</td><td>1010</td><td>10</td><td>1110</td><td>14</td></tr>
	  <tr><td>0011</td><td>3</td><td>0111</td><td>7</td><td>1011</td><td>11</td><td>1111</td><td>15</td></tr>
	</table>

!!! info "Pins 5-8 - Other functions"
	- pins 5-7 are disabled as default  
    - enable power supply (pin 8) is enabled as default - it means the module hw remote switch off is disabled - power supply is allways on. 
	<table>
	  <th>Pin</th><th>Function</th><th>ON</th><th>OFF</th>
	  <tr><td>5</td><td>Gateway RS485-LAN</td><td>Enabled</td><td>Disabled</td></tr>
	  <tr><td>6</td><td>Reserved</td><td>-</td><td>-</td></tr>
	  <tr><td>7</td><td>RS485 terminator</td><td>Enabled</td><td>Disabled</td></tr>
	  <tr><td>8</td><td>Enable power supply</td><td>Enabled</td><td>Disabled</td></tr>
	</table>

<figure markdown="span">
![](media/dipswitch.png){style="max-height:250px;"}
</figure>

## Status LEDs

!!! led-blue "BLUE LED"
	<span class="blue-led-off"></span> - do not lit - no power supply, boot up phase or module failure  
	<span class="blue-led-animation"></span> - flashes 1 Hz - device operating OK  
	<span class="blue-led-on"></span> - lit - awaiting IP address from the DHCP server
!!! led-green "GREEN LED"
	<span class="green-led-off"></span> - do not lit - no ongoing communication - device operating OK  
	<span class="green-led-animation"></span> - flashes - device communicating   
	<span class="green-led-on"></span> - lit - communication failure
