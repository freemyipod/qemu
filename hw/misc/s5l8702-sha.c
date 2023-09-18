#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-sha.h"

#define REG_INDEX(offset) (offset / sizeof(uint32_t))

static uint64_t swapLong(void *X) {
    uint64_t x = (uint64_t) X;
    x = (x & 0x00000000FFFFFFFF) << 32 | (x & 0xFFFFFFFF00000000) >> 32;
    x = (x & 0x0000FFFF0000FFFF) << 16 | (x & 0xFFFF0000FFFF0000) >> 16;
    x = (x & 0x00FF00FF00FF00FF) << 8  | (x & 0xFF00FF00FF00FF00) >> 8;
    return x;
}

static void flush_hw_buffer(S5L8702ShaState *s) {
    // Flush the hardware buffer to the state buffer and clear the buffer.
    memcpy(s->buffer + s->buffer_ind, (uint8_t *)s->hw_buffer, 0x40);
    memset(s->hw_buffer, 0, 0x40);
    s->hw_buffer_dirty = false;
    s->buffer_ind += 0x40;
}

static void sha1_reset(S5L8702ShaState *s) {
	s->config = 0;
	s->memory_start = 0;
	s->memory_mode = 0;
	s->insize = 0;
	memset(&s->buffer, 0, SHA1_BUFFER_SIZE);
	memset(&s->hw_buffer, 0, 0x10 * sizeof(uint32_t));
	s->buffer_ind = 0;
	memset(&s->hashout, 0, 0x14);
	s->hw_buffer_dirty = false;
	s->hash_computed = false;
}

static uint64_t s5l8702_sha_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    S5L8702ShaState *s = S5L8702_SHA(opaque);
    
    switch(offset) {
		case SHA_CONFIG:
			return s->config;
		case SHA_RESET:
			return 0;
		case SHA_MEMORY_START:
			return s->memory_start;
		case SHA_MEMORY_MODE:
			return s->memory_mode;
		case SHA_INSIZE:
			return s->insize;
		/* Hash result ouput */
		case 0x20 ... 0x34:
			//fprintf(stderr, "Hash out %08x\n",  *(uint32_t *)&s->hashout[offset - 0x20]);
            if(!s->hash_computed) {
                // lazy compute the final hash by inspecting the last eight bytes of the buffer, which contains the length of the input data.
                uint64_t data_length = swapLong(((uint64_t *)s->buffer)[s->buffer_ind / 8 - 1]) / 8;

                SHA_CTX ctx;
                SHA1_Init(&ctx);
                SHA1_Update(&ctx, s->buffer, data_length);
                SHA1_Final(s->hashout, &ctx);
                s->hash_computed = true;
            }

			return *(uint32_t *)&s->hashout[offset - 0x20];
	}

    return 0;
}

static void s5l8702_sha_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    S5L8702ShaState *s = S5L8702_SHA(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch(offset) {
		case SHA_CONFIG:
			if(value == 0x2 || value == 0xa)
			{
                if(s->hw_buffer_dirty) {
                    flush_hw_buffer(s);
                }

				if(s->memory_mode)
				{
					// we are in memory mode - gradually add the memory to the buffer
					for(int i = 0; i < s->insize / 0x40; i++) {
						cpu_physical_memory_read(s->memory_start + i * 0x40, s->buffer + s->buffer_ind, 0x40);
						s->buffer_ind += 0x40;
					}
				}
			} else {
				s->config = value;
			}
			break;
		case SHA_RESET:
			sha1_reset(s);
			break;
		case SHA_MEMORY_START:
			s->memory_start = value;
			break;
		case SHA_MEMORY_MODE:
			s->memory_mode = value;
			break;
		case SHA_INSIZE:
            assert(value <= SHA1_BUFFER_SIZE);
			s->insize = value;
			break;
		case 0x40 ... 0x7c:
            // write to the hardware buffer
            s->hw_buffer[(offset - 0x40) / 4] |= value;
            s->hw_buffer_dirty = true;
			break;
	}
}

static const MemoryRegionOps s5l8702_sha_ops = {
    .read = s5l8702_sha_read,
    .write = s5l8702_sha_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    // .valid = {
    //     .min_access_size = 4,
    //     .max_access_size = 4,
    // },
    // .impl.min_access_size = 4,
};

static void s5l8702_sha_reset(DeviceState *dev)
{
    S5L8702ShaState *s = S5L8702_SHA(dev);

    printf("s5l8702_sha_reset\n");

    /* Reset registers */
    // memset(s->regs, 0, sizeof(s->regs));

    /* Set default values for registers */

}

static void s5l8702_sha_init(Object *obj)
{
    S5L8702ShaState *s = S5L8702_SHA(obj);

    printf("s5l8702_sha_init\n");

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_sha_ops, s, TYPE_S5L8702_SHA, S5L8702_SHA_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void s5l8702_sha_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_sha_reset;
}

static const TypeInfo s5l8702_sha_types[] = {
    {
        .name = TYPE_S5L8702_SHA,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_init = s5l8702_sha_init,
        .instance_size = sizeof(S5L8702ShaState),
        .class_init = s5l8702_sha_class_init,
    },
};
DEFINE_TYPES(s5l8702_sha_types);
