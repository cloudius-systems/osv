#include "drivers/virtio.hh"
#include "drivers/virtio-net.hh"

#include "debug.hh"

using namespace pci;


VirtioNet::VirtioNet()
    : Virtio(0x1000)
{

}

VirtioNet::~VirtioNet()
{

}

bool VirtioNet::Init(Device *d)
{
    Virtio::Init(d);
    
    add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

    return true;
}


