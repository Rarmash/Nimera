#include <nimera/block.h>
#include <nimera/mmu.h>
#include <nimera/panic.h>
#include <nimera/pmm.h>
#include <nimera/virtio.h>
#include <nimera/console.h>
#include <nimera/format.h>

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

struct desc { u64 address; u32 length; u16 flags; u16 next; } __attribute__((packed));
struct avail { u16 flags,index,rings[8]; } __attribute__((packed));
struct used_elem { u32 id,len; } __attribute__((packed));
struct used { u16 flags,index; struct used_elem ring[8]; } __attribute__((packed));
struct request { u32 type; u32 reserved; u64 sector; } __attribute__((packed));
struct virtio_state { volatile u8 *base; u16 qsize; u64 desc,avail,used,request,data; u16 last_used, avail_index; };
static struct virtio_state state;
static struct block_device disk;

static u32 rd32(volatile u8 *b,u32 o){return *(volatile u32 *)(b+o);}
static void wr32(volatile u8 *b,u32 o,u32 v){*(volatile u32 *)(b+o)=v;}
static u64 rd64(volatile u8 *b,u32 o){return (u64)rd32(b,o)|(u64)rd32(b,o+4)<<32;}
/* The device reads the queue after the notify write; DSB makes prior RAM stores visible first. */
static void barrier(void){__asm__ volatile("dsb sy" ::: "memory");}
static void clear_page(u64 p){u8 *x=(u8 *)(unsigned long)p;for(u64 i=0;i<NIMERA_PAGE_SIZE;++i)x[i]=0;}
static void set_status(u32 v){wr32(state.base,V_STATUS,v);}
static u32 status(void){return rd32(state.base,V_STATUS);}
static enum block_result io(struct block_device *d,u64 block,void *buffer,int write)
{
	struct request *r=(struct request *)(unsigned long)state.request; struct desc *q=(struct desc *)(unsigned long)state.desc; struct avail *a=(struct avail *)(unsigned long)state.avail; volatile struct used *u=(volatile struct used *)(unsigned long)state.used;
	(void)d; if (block>=disk.block_count) return BLOCK_INVALID;
	r->type=write?1U:0U; r->reserved=0; r->sector=block; for(u64 i=0;i<512;++i)((u8 *)(unsigned long)state.data)[i]=write?((const u8 *)buffer)[i]:0;
	q[0]=(struct desc){state.request,16U,DESC_NEXT,1}; q[1]=(struct desc){state.data,512U,(u16)(write?0:DESC_WRITE)|DESC_NEXT,2}; q[2]=(struct desc){state.request+16U,1U,write?DESC_WRITE:DESC_WRITE,0};
	a->rings[state.avail_index % state.qsize]=0; barrier(); ++state.avail_index; a->index=state.avail_index; barrier(); wr32(state.base,V_NOTIFY,0); barrier();
	for (u64 spins=0ULL; u->index==state.last_used; ++spins) {
		if ((rd32(state.base,V_STATUS)&0x80U)!=0) return BLOCK_IO_ERROR;
		if (spins == 100000000ULL) {
#if NIMERA_BLOCK_TEST
			console_write("VirtIO completion timeout, status "); format_u64_hex(rd32(state.base,V_STATUS)); console_write(" used "); format_u64_decimal(u->index); console_write("\r\n");
#endif
			return BLOCK_IO_ERROR;
		}
	}
	barrier(); state.last_used=u->index; if(!write) for(u64 i=0;i<512;++i)((u8 *)buffer)[i]=((u8 *)(unsigned long)state.data)[i];
	if (*((volatile u8 *)(unsigned long)(state.request+16U)) != 0U) {
#if NIMERA_BLOCK_TEST
		console_write("VirtIO request status: "); format_u64_decimal(*((volatile u8 *)(unsigned long)(state.request+16U))); console_write(" used length: "); format_u64_decimal(u->ring[0].len); console_write("\r\n");
#endif
		return BLOCK_IO_ERROR;
	}
	return BLOCK_OK;
}
static enum block_result read_block(struct block_device*d,u64 b,void*x){return io(d,b,x,0);}
static enum block_result write_block(struct block_device*d,u64 b,const void*x){return io(d,b,(void *)x,1);}

int virtio_block_init(void)
{
	struct virtio_mmio_info info[32]; unsigned int count=virtio_mmio_discover(info,32);
	for(unsigned int i=0;i<count;++i){
		if(mmu_map_device_range(info[i].base,info[i].size)!=0) panic("VirtIO MMIO mapping failed");
	}
	for(unsigned int i=0;i<count;++i){ volatile u8 *b=(volatile u8 *)(unsigned long)info[i].base;
#if NIMERA_BLOCK_TEST
		if (rd32(b,V_DEVICE_ID) != 0U) { console_write("VirtIO candidate "); format_u64_hex(info[i].base); console_write(" id "); format_u64_decimal(rd32(b,V_DEVICE_ID)); console_write("\r\n"); }
#endif
		if(rd32(b,V_MAGIC)!=0x74726976U||rd32(b,V_VERSION)!=2U||rd32(b,V_DEVICE_ID)!=2U) continue;
		state.base=b; set_status(0); set_status(STATUS_ACK); set_status(STATUS_ACK|STATUS_DRIVER);
		wr32(b,V_DF_SEL,0); u64 features=rd32(b,V_DF); wr32(b,V_DF_SEL,1); features|=(u64)rd32(b,V_DF)<<32; if((features&F_VERSION_1)==0) {set_status(0x80); continue;} wr32(b,V_GF_SEL,0); wr32(b,V_GF,0); wr32(b,V_GF_SEL,1); wr32(b,V_GF,(u32)(F_VERSION_1>>32)); set_status(STATUS_ACK|STATUS_DRIVER|STATUS_FEATURES_OK); if((status()&STATUS_FEATURES_OK)==0){set_status(0x80);continue;}
		wr32(b,V_QSEL,0); u32 max=rd32(b,V_QMAX); state.qsize=(u16)(max<8?max:8); if(state.qsize==0){set_status(0x80);continue;}
		if(pmm_alloc_page(&state.desc)||pmm_alloc_page(&state.avail)||pmm_alloc_page(&state.used)||pmm_alloc_page(&state.request)||pmm_alloc_page(&state.data)) panic("VirtIO queue allocation failed"); clear_page(state.desc);clear_page(state.avail);clear_page(state.used);clear_page(state.request);clear_page(state.data); state.last_used=0; state.avail_index=0;
		wr32(b,V_QNUM,state.qsize); wr32(b,V_QDESC,(u32)state.desc);wr32(b,V_QDESC+4,(u32)(state.desc>>32));wr32(b,V_QDRIVER,(u32)state.avail);wr32(b,V_QDRIVER+4,(u32)(state.avail>>32));wr32(b,V_QDEVICE,(u32)state.used);wr32(b,V_QDEVICE+4,(u32)(state.used>>32));wr32(b,V_QREADY,1); set_status(STATUS_ACK|STATUS_DRIVER|STATUS_FEATURES_OK|STATUS_DRIVER_OK);
		disk=(struct block_device){"disk0",512ULL,rd64(b,V_CONFIG),&state,read_block,write_block};
#if NIMERA_BLOCK_TEST
		console_write("VirtIO magic: "); format_u64_hex(rd32(b,V_MAGIC)); console_write(" version: "); format_u64_decimal(rd32(b,V_VERSION)); console_write(" device: "); format_u64_decimal(rd32(b,V_DEVICE_ID)); console_write(" vendor: "); format_u64_hex(rd32(b,V_VENDOR_ID)); console_write("\r\n");
		console_write("Features: VERSION_1\r\nQueue size: "); format_u64_decimal(state.qsize); console_write(" status: "); format_u64_hex(status()); console_write("\r\n");
		console_write("Desc: "); format_u64_hex(state.desc); console_write(" Avail: "); format_u64_hex(state.avail); console_write(" Used: "); format_u64_hex(state.used); console_write("\r\n");
#endif
		if(block_register(&disk)!=BLOCK_OK) panic("VirtIO block registration failed"); return 0; }
	return -1;
}
