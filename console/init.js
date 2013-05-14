//
// init.js
//
// initialization script for the cli environment
//

// setup networking

$("ifconfig eth0 192.168.122.100 netmask 255.255.255.0 up");
$("route add default gw 192.168.122.1");

