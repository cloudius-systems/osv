/* This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */

#include "drivers/driver.hh"
#include "drivers/pci.hh"
#include "debug.hh"

using namespace pci;

bool Driver::Init(pci_device* dev)
{
	_dev = dev;
    if (!dev) {
    	return false;
    }

    if (!earlyInitChecks()) {
    	return false;
    }

    _dev->set_bus_master(true);

    // Enable MSI-x
    if (_dev->is_msix()) {
        if (_dev->is_intx_enabled()) {
        	_dev->disable_intx();
        }
    }

    debug(fmt("Driver initialized %x:%x") % _vid % _id);
    return true;
}

bool Driver::earlyInitChecks(void)
{
	// No init checks in Driver class
	return (true);
}

void Driver::dump_config(void)
{
	// Todo: implement
}

std::ostream& operator << (std::ostream& out, const Driver& d) {
   out << "Driver dev id=" << d._id << " vid=" << d._vid << std::endl;
   return out;
}
