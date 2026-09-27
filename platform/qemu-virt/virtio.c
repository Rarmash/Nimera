#include <nimera/block.h>
#include <nimera/mmu.h>
#include <nimera/panic.h>
#include <nimera/pmm.h>
#include <nimera/virtio.h>
#include <nimera/console.h>
#include <nimera/format.h>
#include <nimera/display.h>

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned short u16;

#define DTB 0x40000000ULL
#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE 2U
#define FDT_PROP 3U
#define FDT_NOP 4U
#define FDT_END 9U
#define MAX_NODES 32U

static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
	((u32)p[2] << 8) | p[3]; }
static u64 cells(const u8 *p, u32 n) { u64 v = 0; if (n == 0U || n > 2U) panic("unsupported Device Tree cells"); for (u32 i=0;i<n;++i) v=(v<<32)|be32(p+i*4U); return v; }
static u32 align4(u32 v) { if (v > 0xfffffffcu) panic("Device Tree alignment overflow"); return (v+3U)&~3U; }
static int eq(const u8 *p, const char *s) { u32 i=0; while (s[i]) { if (p[i]!=(u8)s[i]) return 0; ++i; } return p[i]=='\0'; }

unsigned int virtio_mmio_discover(struct virtio_mmio_info *out, unsigned int cap)
{
	const u8 *dtb=(const u8 *)(unsigned long)DTB, *s, *e, *strings;
	u32 total, so, ss, stro, strs, depth=0, root_ac=2, root_sc=2;
	struct virtio_mmio_info found[MAX_NODES]; unsigned int count=0, node_depth=0;
	int compatible=0, have_reg=0; u64 base=0, size=0, interrupt=0;
	if (be32(dtb)!=0xd00dfeedU) panic("invalid Device Tree magic");
	total=be32(dtb+4); so=be32(dtb+8); stro=be32(dtb+12); strs=be32(dtb+32); ss=be32(dtb+36);
	if (so>total || ss>total-so || stro>total || strs>total-stro) panic("invalid Device Tree bounds");
	s=dtb+so; e=s+ss; strings=dtb+stro;
	while (s<e) {
		u32 token=be32(s); s+=4;
		if (token==FDT_BEGIN_NODE) {
			const u8 *name=s; u32 len=0; while (s<e && *s) {++s;++len;} if (s>=e) panic("unterminated Device Tree node"); ++s; s=dtb+so+align4((u32)(s-(dtb+so))); ++depth;
			if (len>=13U && name[0]=='v' && name[1]=='i' && name[2]=='r' && name[3]=='t' && name[4]=='i' && name[5]=='o' && name[6]=='_') { node_depth=depth; compatible=0; have_reg=0; base=size=interrupt=0; }
		} else if (token==FDT_END_NODE) {
			if (depth==0U) panic("invalid Device Tree depth");
			if (depth==node_depth && compatible && have_reg) { if (count<MAX_NODES) found[count++]=(struct virtio_mmio_info){base,size,interrupt}; node_depth=0; }
			--depth;
		} else if (token==FDT_PROP) {
			u32 len=be32(s), no=be32(s+4); const u8 *value=s+8; const u8 *name;
			if (len>(u32)(e-value) || align4(len)>(u32)(e-value) || no>=strs) panic("invalid Device Tree property");
			name=strings+no; s+=8+align4(len);
			if (depth==1U && eq(name,"#address-cells")) { if(len!=4) panic("invalid address cells"); root_ac=be32(value); }
			else if(depth==1U && eq(name,"#size-cells")) { if(len!=4) panic("invalid size cells"); root_sc=be32(value); }
			else if(depth==node_depth && eq(name,"compatible")) { for(u32 i=0;i+12U<=len;) { if(eq(value+i,"virtio,mmio")) compatible=1; while(i<len && value[i]) ++i; ++i; } }
			else if(depth==node_depth && eq(name,"reg")) { if(root_ac==0||root_sc==0||root_ac>2||root_sc>2||len<(root_ac+root_sc)*4U) panic("invalid virtio reg"); base=cells(value,root_ac); size=cells(value+root_ac*4U,root_sc); have_reg=1; }
			else if(depth==node_depth && eq(name,"interrupts")) { if(len<12U) panic("invalid virtio interrupt"); interrupt=be32(value+8U); }
		} else if(token==FDT_NOP) { } else if(token==FDT_END) { break; } else panic("unknown Device Tree token");
	}
	if (depth!=0U) panic("unbalanced Device Tree structure");
	if (out!= (struct virtio_mmio_info *)0) for(unsigned int i=0;i<count && i<cap;++i) out[i]=found[i];
	return count;
}

/* Modern VirtIO MMIO register offsets and status/descriptor bits. */
#define V_MAGIC 0x000U
#define V_VERSION 0x004U
#define V_DEVICE_ID 0x008U
#define V_VENDOR_ID 0x00cU
#define V_DF_SEL 0x014U
#define V_DF 0x010U
#define V_GF_SEL 0x024U
#define V_GF 0x020U
#define V_QSEL 0x030U
#define V_QMAX 0x034U
#define V_QNUM 0x038U
#define V_QREADY 0x044U
#define V_NOTIFY 0x050U
#define V_STATUS 0x070U
#define V_QDESC 0x080U
#define V_QDRIVER 0x090U
#define V_QDEVICE 0x0a0U
#define V_CONFIG 0x100U
#define STATUS_ACK 1U /* device has seen the driver */
#define STATUS_DRIVER 2U
#define STATUS_FEATURES_OK 8U
#define STATUS_DRIVER_OK 4U
#define F_VERSION_1 (1ULL<<32)
#define DESC_NEXT 1U
#define DESC_WRITE 2U

#define GPU_DEVICE_ID 16U
#define GPU_CMD_GET_DISPLAY_INFO 0x0100U
#define GPU_CMD_RESOURCE_CREATE_2D 0x0101U
#define GPU_CMD_SET_SCANOUT 0x0103U
#define GPU_CMD_RESOURCE_FLUSH 0x0104U
#define GPU_CMD_TRANSFER_TO_HOST_2D 0x0105U
#define GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106U
#define GPU_RESP_OK_NODATA 0x1100U
#define GPU_RESP_OK_DISPLAY_INFO 0x1101U
#define GPU_RESP_ERR 0x1200U
#define GPU_FORMAT_B8G8R8X8 2U

struct desc { u64 address; u32 length; u16 flags; u16 next; } __attribute__((packed));
struct avail { u16 flags,index,rings[8]; } __attribute__((packed));
struct used_elem { u32 id,len; } __attribute__((packed));
struct used { u16 flags,index; struct used_elem ring[8]; } __attribute__((packed));
struct request { u32 type; u32 reserved; u64 sector; } __attribute__((packed));
#define MAX_VIRTIO_BLOCKS 8U
struct virtio_state { volatile u8 *base; u16 qsize; u64 desc,avail,used,request,data; u16 last_used, avail_index; };
static struct virtio_state states[MAX_VIRTIO_BLOCKS];
static struct block_device disks[MAX_VIRTIO_BLOCKS];
static char disk_names[MAX_VIRTIO_BLOCKS][6];
static unsigned int disk_count;

static u32 rd32(volatile u8 *b,u32 o){return *(volatile u32 *)(b+o);}
static void wr32(volatile u8 *b,u32 o,u32 v){*(volatile u32 *)(b+o)=v;}
static u64 rd64(volatile u8 *b,u32 o){return (u64)rd32(b,o)|(u64)rd32(b,o+4)<<32;}
/* The device reads the queue after the notify write; DSB makes prior RAM stores visible first. */
static void barrier(void){__asm__ volatile("dsb sy" ::: "memory");}
static void clear_page(u64 p){u8 *x=(u8 *)(unsigned long)p;for(u64 i=0;i<NIMERA_PAGE_SIZE;++i)x[i]=0;}
static void set_status(struct virtio_state *state,u32 v){wr32(state->base,V_STATUS,v);}
static u32 status(struct virtio_state *state){return rd32(state->base,V_STATUS);}
static enum block_result io(struct block_device *d,u64 block,void *buffer,int write)
{
	struct virtio_state *state=(struct virtio_state *)d->private_data; struct request *r=(struct request *)(unsigned long)state->request; struct desc *q=(struct desc *)(unsigned long)state->desc; struct avail *a=(struct avail *)(unsigned long)state->avail; volatile struct used *u=(volatile struct used *)(unsigned long)state->used;
	if (block>=d->block_count) return BLOCK_INVALID;
	r->type=write?1U:0U; r->reserved=0; r->sector=block; for(u64 i=0;i<512;++i)((u8 *)(unsigned long)state->data)[i]=write?((const u8 *)buffer)[i]:0;
	q[0]=(struct desc){state->request,16U,DESC_NEXT,1}; q[1]=(struct desc){state->data,512U,(u16)(write?0:DESC_WRITE)|DESC_NEXT,2}; q[2]=(struct desc){state->request+16U,1U,write?DESC_WRITE:DESC_WRITE,0};
	a->rings[state->avail_index % state->qsize]=0; barrier(); ++state->avail_index; a->index=state->avail_index; barrier(); wr32(state->base,V_NOTIFY,0); barrier();
	for (u64 spins=0ULL; u->index==state->last_used; ++spins) {
		if ((rd32(state->base,V_STATUS)&0x80U)!=0) return BLOCK_IO_ERROR;
		if (spins == 100000000ULL) {
#if NIMERA_BLOCK_TEST
			console_write("VirtIO completion timeout, status "); format_u64_hex(rd32(state->base,V_STATUS)); console_write(" used "); format_u64_decimal(u->index); console_write("\r\n");
#endif
			return BLOCK_IO_ERROR;
		}
	}
	barrier(); state->last_used=u->index; if(!write) for(u64 i=0;i<512;++i)((u8 *)buffer)[i]=((u8 *)(unsigned long)state->data)[i];
	if (*((volatile u8 *)(unsigned long)(state->request+16U)) != 0U) {
#if NIMERA_BLOCK_TEST
		console_write("VirtIO request status: "); format_u64_decimal(*((volatile u8 *)(unsigned long)(state->request+16U))); console_write(" used length: "); format_u64_decimal(u->ring[0].len); console_write("\r\n");
#endif
		return BLOCK_IO_ERROR;
	}
	return BLOCK_OK;
}
static enum block_result read_block(struct block_device*d,u64 b,void*x){return io(d,b,x,0);}
static enum block_result write_block(struct block_device*d,u64 b,const void*x){return io(d,b,(void *)x,1);}

int virtio_block_init(void)
{
	struct virtio_mmio_info info[32]; unsigned int count=virtio_mmio_discover(info,32); disk_count=0U;
	for(unsigned int i=0;i<count;++i){
		if(mmu_map_device_range(info[i].base,info[i].size)!=0) panic("VirtIO MMIO mapping failed");
	}
	for(unsigned int i=0;i<count && disk_count<MAX_VIRTIO_BLOCKS;++i){ volatile u8 *b=(volatile u8 *)(unsigned long)info[i].base; struct virtio_state *state=&states[disk_count];
#if NIMERA_BLOCK_TEST
		if (rd32(b,V_DEVICE_ID) != 0U) { console_write("VirtIO candidate "); format_u64_hex(info[i].base); console_write(" id "); format_u64_decimal(rd32(b,V_DEVICE_ID)); console_write("\r\n"); }
#endif
		if(rd32(b,V_MAGIC)!=0x74726976U||rd32(b,V_VERSION)!=2U||rd32(b,V_DEVICE_ID)!=2U) continue;
		state->base=b; set_status(state,0); set_status(state,STATUS_ACK); set_status(state,STATUS_ACK|STATUS_DRIVER);
		wr32(b,V_DF_SEL,0); u64 features=rd32(b,V_DF); wr32(b,V_DF_SEL,1); features|=(u64)rd32(b,V_DF)<<32; if((features&F_VERSION_1)==0) {set_status(state,0x80); continue;} wr32(b,V_GF_SEL,0); wr32(b,V_GF,0); wr32(b,V_GF_SEL,1); wr32(b,V_GF,(u32)(F_VERSION_1>>32)); set_status(state,STATUS_ACK|STATUS_DRIVER|STATUS_FEATURES_OK); if((status(state)&STATUS_FEATURES_OK)==0){set_status(state,0x80);continue;}
		wr32(b,V_QSEL,0); u32 max=rd32(b,V_QMAX); state->qsize=(u16)(max<8?max:8); if(state->qsize==0){set_status(state,0x80);continue;}
		if(pmm_alloc_page(&state->desc)||pmm_alloc_page(&state->avail)||pmm_alloc_page(&state->used)||pmm_alloc_page(&state->request)||pmm_alloc_page(&state->data)) panic("VirtIO queue allocation failed"); clear_page(state->desc);clear_page(state->avail);clear_page(state->used);clear_page(state->request);clear_page(state->data); state->last_used=0; state->avail_index=0;
		wr32(b,V_QNUM,state->qsize); wr32(b,V_QDESC,(u32)state->desc);wr32(b,V_QDESC+4,(u32)(state->desc>>32));wr32(b,V_QDRIVER,(u32)state->avail);wr32(b,V_QDRIVER+4,(u32)(state->avail>>32));wr32(b,V_QDEVICE,(u32)state->used);wr32(b,V_QDEVICE+4,(u32)(state->used>>32));wr32(b,V_QREADY,1); set_status(state,STATUS_ACK|STATUS_DRIVER|STATUS_FEATURES_OK|STATUS_DRIVER_OK);
		disks[disk_count]=(struct block_device){disk_names[disk_count],512ULL,rd64(b,V_CONFIG),state,read_block,write_block};
		disk_names[disk_count][0]='d'; disk_names[disk_count][1]='i'; disk_names[disk_count][2]='s'; disk_names[disk_count][3]='k'; disk_names[disk_count][4]=(char)('0'+disk_count); disk_names[disk_count][5]='\0';
#if NIMERA_BLOCK_TEST
		console_write("VirtIO magic: "); format_u64_hex(rd32(b,V_MAGIC)); console_write(" version: "); format_u64_decimal(rd32(b,V_VERSION)); console_write(" device: "); format_u64_decimal(rd32(b,V_DEVICE_ID)); console_write(" vendor: "); format_u64_hex(rd32(b,V_VENDOR_ID)); console_write("\r\n");
		console_write("Features: VERSION_1\r\nQueue size: "); format_u64_decimal(state->qsize); console_write(" status: "); format_u64_hex(status(state)); console_write("\r\n");
		console_write("Desc: "); format_u64_hex(state->desc); console_write(" Avail: "); format_u64_hex(state->avail); console_write(" Used: "); format_u64_hex(state->used); console_write("\r\n");
#endif
		if(block_register(&disks[disk_count])!=BLOCK_OK) panic("VirtIO block registration failed"); ++disk_count; }
	return disk_count == 0U ? -1 : 0;
}

struct gpu_header { u32 type, flags; u64 fence; u32 context, ring; } __attribute__((packed));
struct gpu_rect { u32 x, y, width, height; } __attribute__((packed));
struct gpu_state { volatile u8 *base; u16 qsize; u64 desc, avail, used, request, response; u16 last_used, avail_index; u64 framebuffer, framebuffer_pages; u64 width, height, pitch; };
static struct gpu_state gpu;

static void gpu_zero(u64 address, u64 size) { u8 *p = (u8 *)(unsigned long)address; for (u64 i = 0; i < size; ++i) p[i] = 0; }
static void gpu_release(void)
{
	if (gpu.base != (volatile u8 *)0) wr32(gpu.base, V_STATUS, 0);
	for (u64 i = 0; i < gpu.framebuffer_pages; ++i)
		pmm_free_page(gpu.framebuffer + i * NIMERA_PAGE_SIZE);
	if (gpu.desc != 0ULL) pmm_free_page(gpu.desc);
	if (gpu.avail != 0ULL) pmm_free_page(gpu.avail);
	if (gpu.used != 0ULL) pmm_free_page(gpu.used);
	if (gpu.request != 0ULL) pmm_free_page(gpu.request);
	if (gpu.response != 0ULL) pmm_free_page(gpu.response);
	gpu = (struct gpu_state){0};
}
static int gpu_setup(volatile u8 *base)
{
	u64 features;
	gpu = (struct gpu_state){0};
	gpu.base = base;
	wr32(base, V_STATUS, 0); wr32(base, V_STATUS, STATUS_ACK | STATUS_DRIVER);
	wr32(base, V_DF_SEL, 0); features = rd32(base, V_DF);
	wr32(base, V_DF_SEL, 1); features |= (u64)rd32(base, V_DF) << 32;
	if ((features & F_VERSION_1) == 0ULL) return -1;
	wr32(base, V_GF_SEL, 0); wr32(base, V_GF, 0);
	wr32(base, V_GF_SEL, 1); wr32(base, V_GF, (u32)(F_VERSION_1 >> 32));
	wr32(base, V_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK);
	if ((rd32(base, V_STATUS) & STATUS_FEATURES_OK) == 0U) return -1;
	wr32(base, V_QSEL, 0); gpu.qsize = (u16)rd32(base, V_QMAX);
	if (gpu.qsize == 0U) return -1;
	if (gpu.qsize > 8U) gpu.qsize = 8U;
	if (pmm_alloc_page(&gpu.desc) != 0) return -1;
	if (pmm_alloc_page(&gpu.avail) != 0) { gpu_release(); return -1; }
	if (pmm_alloc_page(&gpu.used) != 0) { gpu_release(); return -1; }
	if (pmm_alloc_page(&gpu.request) != 0) { gpu_release(); return -1; }
	if (pmm_alloc_page(&gpu.response) != 0) { gpu_release(); return -1; }
	gpu_zero(gpu.desc, NIMERA_PAGE_SIZE); gpu_zero(gpu.avail, NIMERA_PAGE_SIZE);
	gpu_zero(gpu.used, NIMERA_PAGE_SIZE); gpu_zero(gpu.request, NIMERA_PAGE_SIZE); gpu_zero(gpu.response, NIMERA_PAGE_SIZE);
	wr32(base, V_QNUM, gpu.qsize); wr32(base, V_QDESC, (u32)gpu.desc); wr32(base, V_QDESC + 4, (u32)(gpu.desc >> 32));
	wr32(base, V_QDRIVER, (u32)gpu.avail); wr32(base, V_QDRIVER + 4, (u32)(gpu.avail >> 32));
	wr32(base, V_QDEVICE, (u32)gpu.used); wr32(base, V_QDEVICE + 4, (u32)(gpu.used >> 32)); wr32(base, V_QREADY, 1);
	wr32(base, V_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK | STATUS_DRIVER_OK);
	gpu.last_used = 0; gpu.avail_index = 0;
	return 0;
}

static int gpu_submit(u32 request_length, u32 response_length)
{
	struct desc *q = (struct desc *)(unsigned long)gpu.desc;
	struct avail *a = (struct avail *)(unsigned long)gpu.avail;
	volatile struct used *u = (volatile struct used *)(unsigned long)gpu.used;
	struct desc request = {gpu.request, request_length, DESC_NEXT, 1};
	struct desc response = {gpu.response, response_length, DESC_WRITE, 0};
	q[0] = request; q[1] = response; a->rings[gpu.avail_index % gpu.qsize] = 0;
	barrier(); ++gpu.avail_index; a->index = gpu.avail_index; barrier(); wr32(gpu.base, V_NOTIFY, 0); barrier();
	for (u64 spins = 0ULL; u->index == gpu.last_used; ++spins) {
		if ((rd32(gpu.base, V_STATUS) & 0x80U) != 0U || spins == 100000000ULL) return -1;
	}
	barrier(); gpu.last_used = u->index;
	return ((struct gpu_header *)(unsigned long)gpu.response)->type == GPU_RESP_ERR ? -1 : 0;
}

static void gpu_put32(u8 *p, u32 value) { p[0] = (u8)value; p[1] = (u8)(value >> 8); p[2] = (u8)(value >> 16); p[3] = (u8)(value >> 24); }
static void gpu_put64(u8 *p, u64 value) { gpu_put32(p, (u32)value); gpu_put32(p + 4, (u32)(value >> 32)); }
static int gpu_command(u32 type, u32 request_length, u32 response_length)
{
	struct gpu_header *header = (struct gpu_header *)(unsigned long)gpu.request;
	gpu_zero(gpu.request, NIMERA_PAGE_SIZE); gpu_zero(gpu.response, NIMERA_PAGE_SIZE);
	header->type = type;
	(void)request_length; (void)response_length;
	return 0;
}

static int gpu_flush(u64 x, u64 y, u64 width, u64 height)
{
	u8 *request;
	if (x >= gpu.width || y >= gpu.height) return -1;
	if (width > gpu.width - x) width = gpu.width - x;
	if (height > gpu.height - y) height = gpu.height - y;
	request = (u8 *)(unsigned long)gpu.request;
	if (gpu_command(GPU_CMD_TRANSFER_TO_HOST_2D, 56U, 24U) != 0) return -1;
	gpu_put32(request + 24, (u32)x); gpu_put32(request + 28, (u32)y); gpu_put32(request + 32, (u32)width); gpu_put32(request + 36, (u32)height);
	gpu_put64(request + 40, 0ULL); gpu_put32(request + 48, 1U);
	if (gpu_submit(56U, 24U) != 0) return -1;
	if (gpu_command(GPU_CMD_RESOURCE_FLUSH, 48U, 24U) != 0) return -1;
	gpu_put32(request + 24, (u32)x); gpu_put32(request + 28, (u32)y); gpu_put32(request + 32, (u32)width); gpu_put32(request + 36, (u32)height); gpu_put32(request + 40, 1U);
	return gpu_submit(48U, 24U);
}

int virtio_gpu_init(void)
{
	struct virtio_mmio_info info[32]; u32 width, height; u8 *request;
	unsigned int count = virtio_mmio_discover(info, 32);
	for (unsigned int i = 0; i < count; ++i) {
		volatile u8 *base = (volatile u8 *)(unsigned long)info[i].base;
		if (mmu_map_device_range(info[i].base, info[i].size) != 0) return -1;
		if (rd32(base, V_MAGIC) != 0x74726976U || rd32(base, V_VERSION) != 2U || rd32(base, V_DEVICE_ID) != GPU_DEVICE_ID) continue;
		console_write("virtio-gpu: found "); format_u64_hex(info[i].base); console_write("\r\n");
		if (gpu_setup(base) != 0) return -1;
		if (gpu_command(GPU_CMD_GET_DISPLAY_INFO, 24U, 408U) != 0 || gpu_submit(24U, 408U) != 0) { gpu_release(); return -1; }
		request = (u8 *)(unsigned long)gpu.response;
		width = request[24 + 8] | ((u32)request[24 + 9] << 8) | ((u32)request[24 + 10] << 16) | ((u32)request[24 + 11] << 24);
		height = request[24 + 12] | ((u32)request[24 + 13] << 8) | ((u32)request[24 + 14] << 16) | ((u32)request[24 + 15] << 24);
		if (width == 0U || height == 0U || width > 1920U || height > 1080U) { gpu_release(); return -1; }
		gpu.width = width; gpu.height = height; gpu.pitch = (u64)width * 4ULL;
		gpu.framebuffer_pages = (gpu.pitch * height + NIMERA_PAGE_SIZE - 1ULL) / NIMERA_PAGE_SIZE;
		if (pmm_alloc_contiguous(gpu.framebuffer_pages, &gpu.framebuffer) != 0) { gpu_release(); return -1; }
		gpu_zero(gpu.framebuffer, gpu.framebuffer_pages * NIMERA_PAGE_SIZE);
		console_write("display mode: "); format_u64_decimal(width); console_putc('x'); format_u64_decimal(height); console_write("\r\nframebuffer: allocated\r\n");
		request = (u8 *)(unsigned long)gpu.request; if (gpu_command(GPU_CMD_RESOURCE_CREATE_2D, 40U, 24U) != 0) { gpu_release(); return -1; } gpu_put32(request + 24, 1U); gpu_put32(request + 28, GPU_FORMAT_B8G8R8X8); gpu_put32(request + 32, width); gpu_put32(request + 36, height); if (gpu_submit(40U, 24U) != 0) { gpu_release(); return -1; }
		console_write("resource 2D: created\r\n");
		if (gpu_command(GPU_CMD_RESOURCE_ATTACH_BACKING, 48U, 24U) != 0) { gpu_release(); return -1; }
		gpu_put32(request + 24, 1U); gpu_put32(request + 28, 1U); gpu_put64(request + 32, gpu.framebuffer); gpu_put32(request + 40, (u32)(gpu.pitch * height)); gpu_put32(request + 44, 0U); if (gpu_submit(48U, 24U) != 0) { gpu_release(); return -1; }
		console_write("backing: attached\r\n");
		if (gpu_command(GPU_CMD_SET_SCANOUT, 48U, 24U) != 0) { gpu_release(); return -1; }
		gpu_put32(request + 24, 0U); gpu_put32(request + 28, 0U); gpu_put32(request + 32, width); gpu_put32(request + 36, height); gpu_put32(request + 40, 0U); gpu_put32(request + 44, 1U); if (gpu_submit(48U, 24U) != 0) { gpu_release(); return -1; }
		console_write("scanout: configured\r\n");
		display_register((u32 *)(unsigned long)gpu.framebuffer, width, height, gpu.pitch, DISPLAY_PIXEL_B8G8R8X8, gpu_flush);
		return 0;
	}
	return -1;
}
