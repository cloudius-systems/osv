//
// init.js
//
// initialization script for the cli environment
//

// setup networking

// uncomment to use a static ip address
// $("ifconfig eth0 192.168.122.100 netmask 255.255.255.0 up");
// $("route add default gw 192.168.122.1");


//
// FIXME:
//   1. the setting of IP 0.0.0.0 should be done for each interface
//   2. running dhclient twice is not supported ;)
//
$("ifconfig eth0 0.0.0.0 netmask 255.255.255.0 up");
$("dhclient");
