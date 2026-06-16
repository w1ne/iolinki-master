#include "iolinki_master/master.h"

int iolink_master_read_isdu(iolink_master_port_t* port,
                            uint16_t index,
                            uint8_t subindex,
                            uint8_t* data,
                            uint8_t* len)
{
    (void)index;
    (void)subindex;

    if((port == NULL) || (data == NULL) || (len == NULL))
    {
        return -1;
    }

    return 1;
}

int iolink_master_write_isdu(iolink_master_port_t* port,
                             uint16_t index,
                             uint8_t subindex,
                             const uint8_t* data,
                             uint8_t len)
{
    (void)index;
    (void)subindex;

    if((port == NULL) || ((data == NULL) && (len > 0U)))
    {
        return -1;
    }

    return 1;
}
