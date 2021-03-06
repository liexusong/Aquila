#include <core/system.h>
#include <core/panic.h>
#include <cpu/io.h>
#include <dev/dev.h>
#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/posix.h>
#include <fs/mbr.h>
#include <bits/errno.h>
#include <dev/pci.h>

#include <ata.h>

static struct ata_device {
    struct ioaddr base;
    struct ioaddr ctrl;
    uint8_t  slave;

    size_t   poff[4];
} __devices[4] = {0};

static void ata_wait(struct ata_device *dev)
{
    for (int i = 0; i < 4; ++i)
        io_in8(&dev->base, ATA_PORT_CMD);
        //inb(dev->base + ATA_PORT_CMD);
}

static void ata_soft_reset(struct ata_device *dev)
{
    //outb(dev->ctrl, ATA_CMD_RESET);
    io_out8(&dev->ctrl, 0, ATA_CMD_RESET);
    ata_wait(dev);
    //outb(dev->ctrl, 0);
    io_out8(&dev->ctrl, 0, 0);
}

static void ata_poll(struct ata_device *dev, int advanced_check)
{
    ata_wait(dev);

    uint8_t s;
    /* Wait until busy clears */
    while ((s = io_in8(&dev->base, ATA_PORT_CMD)) & ATA_STATUS_BSY);

    if (advanced_check) {
        if ((s & ATA_STATUS_ERR) || (s & ATA_STATUS_DF)) {
            ata_wait(dev);
            uint8_t err = io_in8(&dev->base, ATA_PORT_ERROR);
            printk("DRIVE ERROR! %d\n", err);
            panic("");
        }

        if (!(s & ATA_STATUS_DRQ)) {
            panic("DRQ NOT SET\n");
        }
    }
}

static struct ata_device *__last_selected_dev = NULL;
static void ata_select_device(struct ata_device *dev)
{
    if (dev != __last_selected_dev) {
        io_out8(&dev->base, ATA_PORT_DRIVE, ATA_DRIVE_LBA48 | (dev->slave << 4));
        ata_wait(dev);
        __last_selected_dev = dev;
    }
}

static ssize_t ata_read_sectors(struct ata_device *dev, uint64_t lba, size_t count, void *buf)
{
    //printk("ata_read_sectors(dev=%p, lba=%x, count=%d, buf=%p)\n", dev, (uint32_t) lba, count, buf);    /* XXX */
    ata_select_device(dev);

    /* Send NULL byte to error port */
    io_out8(&dev->base, ATA_PORT_ERROR, 0);

    /* Send sectors count and LBA */
    io_out8(&dev->base, ATA_PORT_COUNT, (count >> 8) & 0xFF);
    io_out8(&dev->base, ATA_PORT_LBALO, (uint8_t) (lba >> (8 * 3)));
    io_out8(&dev->base, ATA_PORT_LBAMI, (uint8_t) (lba >> (8 * 4)));
    io_out8(&dev->base, ATA_PORT_LBAHI, (uint8_t) (lba >> (8 * 5)));
    io_out8(&dev->base, ATA_PORT_COUNT, count & 0xFF);
    io_out8(&dev->base, ATA_PORT_LBALO, (uint8_t) (lba >> (8 * 0)));
    io_out8(&dev->base, ATA_PORT_LBAMI, (uint8_t) (lba >> (8 * 1)));
    io_out8(&dev->base, ATA_PORT_LBAHI, (uint8_t) (lba >> (8 * 2)));

    /* Send read command */
    ata_wait(dev);
    io_out8(&dev->base, ATA_PORT_CMD, ATA_CMD_READ_SECOTRS_EXT);

    while (count--) {
        ata_poll(dev, 1);
        /* FIXME */
        __insw(dev->base.addr + ATA_PORT_DATA, 256, buf);
        buf += 512;
    }

    ata_poll(dev, 0);

    return 0;
}

static ssize_t ata_write_sectors(struct ata_device *dev, uint64_t lba, size_t count, void *buf)
{
    //printk("ata_write_sectors(dev=%p, lba=%x, count=%d, buf=%p)\n", dev, (uint32_t) lba, count, buf);    /* XXX */
    ata_select_device(dev);

    /* Send NULL byte to error port */
    io_out8(&dev->base, ATA_PORT_ERROR, 0);

    /* Send sectors count and LBA */
    io_out8(&dev->base, ATA_PORT_COUNT, (count >> 8) & 0xFF);
    io_out8(&dev->base, ATA_PORT_LBALO, (uint8_t) (lba >> (8 * 3)));
    io_out8(&dev->base, ATA_PORT_LBAMI, (uint8_t) (lba >> (8 * 4)));
    io_out8(&dev->base, ATA_PORT_LBAHI, (uint8_t) (lba >> (8 * 5)));
    io_out8(&dev->base, ATA_PORT_COUNT, count & 0xFF);
    io_out8(&dev->base, ATA_PORT_LBALO, (uint8_t) (lba >> (8 * 0)));
    io_out8(&dev->base, ATA_PORT_LBAMI, (uint8_t) (lba >> (8 * 1)));
    io_out8(&dev->base, ATA_PORT_LBAHI, (uint8_t) (lba >> (8 * 2)));

    /* Send write command */
    ata_wait(dev);
    io_out8(&dev->base, ATA_PORT_CMD, ATA_CMD_WRITE_SECOTRS_EXT);

    while (count--) {
        uint16_t *_buf = (uint16_t *) buf;

        ata_poll(dev, 1);
        for (int i = 0; i < 256; ++i)
            io_out16(&dev->base, ATA_PORT_DATA, _buf[i]);

        buf += 512;
    }

    ata_poll(dev, 0);
    io_out8(&dev->base, ATA_PORT_CMD, ATA_CMD_CACHE_FLUSH);
    ata_poll(dev, 0);

    return 0;
}

#define BLOCK_SIZE  512UL
static char read_buf[BLOCK_SIZE] __aligned(16);

static size_t ata_partition_offset(struct ata_device *dev, size_t p)
{
    if (!p) /* Whole Disk */
        return 0;

    p -= 1;

    if (dev->poff[p] != 0)
        return dev->poff[p];

    mbr_t *mbr;

    ata_read_sectors(dev, 0, 1, read_buf);
    mbr = (mbr_t *) read_buf;

    return dev->poff[p] = mbr->ptab[p].start_lba;
}

static ssize_t ata_read(struct devid *dd, off_t offset, size_t size, void *buf)
{
    //printk("ata_read(dd=%p, offset=%d, size=%d, buf=%p)\n", dd, offset, size, buf);
    ssize_t ret = 0;
    char *_buf = buf;

    size_t dev_id = dd->minor / 64;
    size_t partition = dd->minor % 64;

    struct ata_device *dev = &__devices[dev_id];
    offset += ata_partition_offset(dev, partition);

    ata_read_sectors(dev, offset, size, buf);
    return size;
}

static ssize_t ata_write(struct devid *dd, off_t offset, size_t size, void *buf)
{
    ssize_t ret = 0;
    char *_buf = buf;

    size_t dev_id = dd->minor / 64;
    size_t partition = dd->minor % 64;

    struct ata_device *dev = &__devices[dev_id];
    offset += ata_partition_offset(dev, partition);

    ata_write_sectors(dev, offset, size, buf);
    return size;
}

uint8_t ata_detect_device(struct ata_device *dev)
{
    ata_select_device(dev);

    uint8_t status = io_in8(&dev->base, ATA_PORT_CMD);
    if (!status) /* No Device, bail */
        return ATADEV_UNKOWN;

    uint8_t type_low  = io_in8(&dev->base, ATA_PORT_LBAMI);
    uint8_t type_high = io_in8(&dev->base, ATA_PORT_LBAHI);

    if (type_low == 0x14 && type_high == 0xEB)
        return ATADEV_PATAPI;
    if (type_low == 0x69 && type_high == 0x96)
        return ATADEV_SATAPI;
    if (type_low == 0 && type_high == 0)
        return ATADEV_PATA;
    if (type_low == 0x3c && type_high == 0xc3)
        return ATADEV_SATA;
    
    return ATADEV_UNKOWN;
}

//static char pata_idx = 'a';
void pata_init(struct ata_device *dev)
{
    //char name[] = "hda";
    //name[2] = pata_idx;
    ata_soft_reset(dev);
    //readmbr(hd);
}

int ata_probe()
{
#if 0
    struct pci_dev ide;
    if (pci_scan_device(0x01, 0x01, &ide)) {
        panic("No IDE controller found\n"); /* FIXME */
    }
#endif

    __devices[0].base.addr = ATA_PIO_PORT_BASE;
    __devices[0].base.type = IOADDR_PORT;
    __devices[0].ctrl.addr = ATA_PIO_PORT_CONTROL;
    __devices[0].ctrl.type = IOADDR_PORT;

    uint8_t type = ata_detect_device(&__devices[0]);

    switch (type) {
        case ATADEV_PATA:
            pata_init(&__devices[0]);
            break;
    }

    kdev_blkdev_register(3, &atadev);
    return 0;
}

size_t ata_getbs(struct devid *dd __unused)
{
    return BLOCK_SIZE;
}

struct dev atadev = {
    .probe = ata_probe,
    .read  = ata_read,
    .write = ata_write,
    .getbs = ata_getbs,

    .fops = {
        .open  = posix_file_open,
        .read  = posix_file_read,
        .write = posix_file_write,
    },
};

MODULE_INIT(ata, ata_probe, NULL)
