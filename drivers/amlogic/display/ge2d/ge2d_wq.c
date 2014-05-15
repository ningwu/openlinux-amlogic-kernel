/*******************************************************************
 *
 *  Copyright C 2007 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 2009/12/31   19:46
 *
 *******************************************************************/


#include <linux/ge2d/ge2d.h>
#include <linux/interrupt.h>
#include <mach/am_regs.h>
#include <linux/amports/canvas.h>
#include <linux/fb.h>
#include <linux/list.h>
#include  <linux/spinlock.h>
#include <linux/kthread.h>
#include "ge2d_log.h"
#include <linux/amlog.h>
static  ge2d_manager_t  ge2d_manager;


static int   get_queue_member_count(struct list_head  *head)
{
	int member_count=0;
	ge2d_queue_item_t *pitem;
	list_for_each_entry(pitem, head, list){
		member_count++;
		if(member_count>MAX_GE2D_CMD)//error has occured
		break;	
	}
	return member_count;
}
ssize_t work_queue_status_show(struct class *cla,struct class_attribute *attr,char *buf)
{
	ge2d_context_t *wq=ge2d_manager.current_wq;
     	return snprintf(buf,40,"cmd count in queue:%d\n",get_queue_member_count(&wq->work_queue));
}
ssize_t free_queue_status_show(struct class *cla,struct class_attribute *attr, char *buf)
{
	ge2d_context_t *wq=ge2d_manager.current_wq;
     	return snprintf(buf, 40, "free space :%d\n",get_queue_member_count(&wq->free_queue));
}

static inline  int  work_queue_no_space(ge2d_context_t* queue)
{
	return  list_empty(&queue->free_queue) ;
}

static int ge2d_process_work_queue(ge2d_context_t *  wq)
{
	ge2d_config_t *cfg;
	ge2d_queue_item_t *pitem;
	unsigned int  mask=0x1;
	struct list_head  *head=&wq->work_queue,*pos;
	int ret=0;
	unsigned int block_mode;

	ge2d_manager.ge2d_state=GE2D_STATE_RUNNING;
	pos=head->next;
	if(pos!=head) //current work queue not empty.
	{
		if(wq!=ge2d_manager.last_wq )//maybe 
		{
			pitem=(ge2d_queue_item_t *)pos;  //modify the first item .
			if(pitem)
			pitem->config.update_flag=UPDATE_ALL;
			else{
				amlog_mask_level(LOG_MASK_WORK,LOG_LEVEL_HIGH,"can't get pitem\r\n");	
				ret=-1;
				goto  exit;
			}
		}else{ 
			pitem=(ge2d_queue_item_t *)pos;  //modify the first item .
		}
		
	}else{
		ret =-1;
		goto  exit;
	}

	do{
		
	      	cfg = &pitem->config;
		mask=0x1;	
            	while(cfg->update_flag && mask <= UPDATE_SCALE_COEF ) //we do not change 
		{
			switch(cfg->update_flag & mask)
			{
				case UPDATE_SRC_DATA:
				ge2d_set_src1_data(&cfg->src1_data);
				break;
				case UPDATE_SRC_GEN:
				ge2d_set_src1_gen(&cfg->src1_gen);
				break;
				case UPDATE_DST_DATA:
				ge2d_set_src2_dst_data(&cfg->src2_dst_data);	
				break;
				case UPDATE_DST_GEN:
				ge2d_set_src2_dst_gen(&cfg->src2_dst_gen);
				break;
				case UPDATE_DP_GEN:
				ge2d_set_dp_gen(&cfg->dp_gen);
				break;
				case UPDATE_SCALE_COEF:
				ge2d_set_src1_scale_coef(cfg->v_scale_coef_type, cfg->h_scale_coef_type);
				break;
			}

			cfg->update_flag &=~mask;
			mask = mask <<1 ;
		
		}
            	ge2d_set_cmd(&pitem->cmd);//set START_FLAG in this func.
      		//remove item
      		block_mode=pitem->cmd.wait_done_flag;
      		spin_lock(&wq->lock);
		pos=pos->next;	
		list_move_tail(&pitem->list,&wq->free_queue);
		spin_unlock(&wq->lock);
		
		while(ge2d_is_busy())
		interruptible_sleep_on_timeout(&ge2d_manager.event.cmd_complete, 1);
		//if block mode (cmd)
		if(block_mode)
		{
			wake_up_interruptible(&wq->cmd_complete);
		}
		pitem=(ge2d_queue_item_t *)pos;
	}while(pos!=head);
	ge2d_manager.last_wq=wq;
exit:
	if(ge2d_manager.ge2d_state==GE2D_STATE_REMOVING_WQ)
	complete(&ge2d_manager.event.process_complete);
	ge2d_manager.ge2d_state=GE2D_STATE_IDLE;
	return ret;	
}

                    
static irqreturn_t ge2d_wq_handle(int  irq_number, void *para)
{
	wake_up(&ge2d_manager.event.cmd_complete) ;
	return IRQ_HANDLED;


}


ge2d_src1_data_t *ge2d_wq_get_src_data(ge2d_context_t *wq)
{
    	return &wq->config.src1_data;
}

ge2d_src1_gen_t *ge2d_wq_get_src_gen(ge2d_context_t *wq)
{
   	return &wq->config.src1_gen;
}

ge2d_src2_dst_data_t *ge2d_wq_get_dst_data(ge2d_context_t *wq)
{
	return &wq->config.src2_dst_data;
}

ge2d_src2_dst_gen_t *ge2d_wq_get_dst_gen(ge2d_context_t *wq)
{
	return &wq->config.src2_dst_gen;
}

ge2d_dp_gen_t * ge2d_wq_get_dp_gen(ge2d_context_t *wq)
{
	return &wq->config.dp_gen;
}

ge2d_cmd_t * ge2d_wq_get_cmd(ge2d_context_t *wq)
{
	return &wq->cmd;
}

void ge2d_wq_set_scale_coef(ge2d_context_t *wq, unsigned v_scale_coef_type, unsigned h_scale_coef_type)
{
    	
    	if (wq){
        wq->config.v_scale_coef_type = v_scale_coef_type;
        wq->config.h_scale_coef_type = h_scale_coef_type;
        wq->config.update_flag |= UPDATE_SCALE_COEF;
    	}
}
 
/*********************************************************************
**
**
** each  process has it's single  ge2d  op point 
**
**
**********************************************************************/
int ge2d_wq_add_work(ge2d_context_t *wq)
{

	ge2d_queue_item_t  *pitem ;
    
     	amlog_mask_level(LOG_MASK_WORK,LOG_LEVEL_LOW,"add new work @@%s:%d\r\n",__func__,__LINE__)	; 
 	if(work_queue_no_space(wq))
 	{
 		amlog_mask_level(LOG_MASK_WORK,LOG_LEVEL_LOW,"work queue no space\r\n");
		//we should wait for queue empty at this point.
		while(work_queue_no_space(wq))
		{
			interruptible_sleep_on_timeout(&ge2d_manager.event.cmd_complete, 3);
		}
		amlog_mask_level(LOG_MASK_WORK,LOG_LEVEL_LOW,"got free space\r\n");
	}

      pitem=list_entry(wq->free_queue.next,ge2d_queue_item_t,list); 
	if(IS_ERR(pitem))
	{
		goto error;
	}
	memcpy(&pitem->cmd, &wq->cmd, sizeof(ge2d_cmd_t));
      	memset(&wq->cmd, 0, sizeof(ge2d_cmd_t));
      	memcpy(&pitem->config, &wq->config, sizeof(ge2d_config_t));
	wq->config.update_flag =0;  //reset config set flag   
	spin_lock(&wq->lock);
	list_move_tail(&pitem->list,&wq->work_queue);
	spin_unlock(&wq->lock);
	amlog_mask_level(LOG_MASK_WORK,LOG_LEVEL_LOW,"add new work ok\r\n"); 
	if(ge2d_manager.event.cmd_in_sem.count == 0 )//only read not need lock
	up(&ge2d_manager.event.cmd_in_sem) ;//new cmd come in	
	//add block mode   if()
	if(pitem->cmd.wait_done_flag)
	{
		interruptible_sleep_on(&wq->cmd_complete);
	}
	return 0;
error:
 	 return -1;	
}






static inline ge2d_context_t*  get_next_work_queue(ge2d_manager_t*  manager)
{
	ge2d_context_t* pcontext;

	spin_lock(&ge2d_manager.event.sem_lock);
	list_for_each_entry(pcontext,&manager->process_queue,list)
	{
		if(!list_empty(&pcontext->work_queue))	//not lock maybe delay to next time.
		{									
			list_move(&manager->process_queue,&pcontext->list);//move head .
			spin_unlock(&ge2d_manager.event.sem_lock);
			return pcontext;	
		}	
	}
	spin_unlock(&ge2d_manager.event.sem_lock);
	return NULL;
}
static int ge2d_monitor_thread(void *data)
{

	ge2d_manager_t*  manager = (  ge2d_manager_t*)data ;
	
 	amlog_level(LOG_LEVEL_HIGH,"ge2d workqueue monitor start\r\n");
	//setup current_wq here.
	while(ge2d_manager.process_queue_state!=GE2D_PROCESS_QUEUE_STOP)
	{
		down_timeout(&manager->event.cmd_in_sem,6000);
		//got new cmd arrived in signal,
		while((manager->current_wq=get_next_work_queue(manager))!=NULL)
		{
			ge2d_process_work_queue(manager->current_wq);
		}
		
	}
	amlog_level(LOG_LEVEL_HIGH,"exit ge2d_monitor_thread\r\n");
	return 0;
}
static  int ge2d_start_monitor(void )
{
	int ret =0;
	
	amlog_level(LOG_LEVEL_HIGH,"ge2d start monitor\r\n");
	ge2d_manager.process_queue_state=GE2D_PROCESS_QUEUE_START;
	ge2d_manager.ge2d_thread=kthread_run(ge2d_monitor_thread,&ge2d_manager,"ge2d_monitor");
	if (IS_ERR(ge2d_manager.ge2d_thread)) {
		ret = PTR_ERR(ge2d_manager.ge2d_thread);
		amlog_level(LOG_LEVEL_HIGH,"ge2d monitor : failed to start kthread (%d)\n", ret);
	}
	return ret;
}
static  int  ge2d_stop_monitor(void)
{
	amlog_level(LOG_LEVEL_HIGH,"stop ge2d monitor thread\n");
	ge2d_manager.process_queue_state =GE2D_PROCESS_QUEUE_STOP;
	up(&ge2d_manager.event.cmd_in_sem) ;
	return  0;
}
/********************************************************************
**																		 	**
**																			**
**  >>>>>>>>>>>>>		interface export to other parts	<<<<<<<<<<<<<			**
**																			**
**																			**
*********************************************************************/


/***********************************************************************
** context  setup secion
************************************************************************/
static inline int bpp(unsigned format)
{
	if(format & GE2D_FORMAT_YUV) return 8;
	switch (format & GE2D_BPP_MASK) {
		case GE2D_BPP_8BIT:
			return 8;
		case GE2D_BPP_16BIT:
			return 16;
		case GE2D_BPP_24BIT:
			return 24;
		case GE2D_BPP_32BIT:
		default:
			return 32;
	}
}

static void build_ge2d_config(config_para_t *cfg, src_dst_para_t *src, src_dst_para_t *dst,int index)
{
	index&=0xff;
	if(src)
	{
		src->xres = cfg->src_planes[0].w;
		src->yres = cfg->src_planes[0].h;
//		src->canvas_index = (index+3)<<24|(index+2)<<16|(index+1)<<8|index;
		src->ge2d_color_index = cfg->src_format;
		src->bpp = bpp(cfg->src_format);
		
	    if(cfg->src_planes[0].addr){
	        src->canvas_index = index;
    		canvas_config(index++,
    			  cfg->src_planes[0].addr,
				  cfg->src_planes[0].w * src->bpp / 8,
				  cfg->src_planes[0].h,
                  CANVAS_ADDR_NOWRAP,
		          CANVAS_BLKMODE_LINEAR);
	    }
		/* multi-src_planes */
		if(cfg->src_planes[1].addr){
            src->canvas_index |= index<<8;
            canvas_config(index++,
            		  cfg->src_planes[1].addr,
            		  cfg->src_planes[1].w * src->bpp / 8,
            		  cfg->src_planes[1].h,
                	  CANVAS_ADDR_NOWRAP,
                  	  CANVAS_BLKMODE_LINEAR);
		 }
		 if(cfg->src_planes[2].addr){
		    src->canvas_index |= index<<16;
    		canvas_config(index++,
    				  cfg->src_planes[2].addr,
					  cfg->src_planes[2].w * src->bpp / 8,
					  cfg->src_planes[2].h,
	                  CANVAS_ADDR_NOWRAP,
			          CANVAS_BLKMODE_LINEAR);
        }
        if(cfg->src_planes[3].addr){
            src->canvas_index |= index<<24;
		    canvas_config(index++,
    				  cfg->src_planes[3].addr,
					  cfg->src_planes[3].w * src->bpp / 8,
					  cfg->src_planes[3].h,
                	  CANVAS_ADDR_NOWRAP,
		          	  CANVAS_BLKMODE_LINEAR);
		}
	
	}
	if(dst)
	{
		dst->xres = cfg->dst_planes[0].w;
		dst->yres = cfg->dst_planes[0].h;
//		dst->canvas_index = (index+3)<<24|(index+2)<<16|(index+1)<<8|index;
		dst->ge2d_color_index = cfg->dst_format;
		dst->bpp = bpp(cfg->dst_format);
		if(cfg->dst_planes[0].addr){
		    dst->canvas_index = index;
		    canvas_config(index++ & 0xff,
			  cfg->dst_planes[0].addr,
			  cfg->dst_planes[0].w * dst->bpp / 8,
			  cfg->dst_planes[0].h,
              CANVAS_ADDR_NOWRAP,
	          CANVAS_BLKMODE_LINEAR);
	    }


		/* multi-src_planes */
        if(cfg->dst_planes[1].addr){
            dst->canvas_index |= index<<8;
            canvas_config(index++,
                  cfg->dst_planes[1].addr,
                  cfg->dst_planes[1].w * dst->bpp / 8,
                  cfg->dst_planes[1].h,
                  CANVAS_ADDR_NOWRAP,
                  CANVAS_BLKMODE_LINEAR);
        }
        if(cfg->dst_planes[2].addr){
            dst->canvas_index |= index<<16;	          	
            canvas_config(index++,
                cfg->dst_planes[2].addr,
                cfg->dst_planes[2].w * dst->bpp / 8,
                cfg->dst_planes[2].h,
                CANVAS_ADDR_NOWRAP,
                  CANVAS_BLKMODE_LINEAR);
        }
        if(cfg->dst_planes[3].addr){
            dst->canvas_index |= index<<24;			        
            canvas_config(index++,
        		  cfg->dst_planes[3].addr,
        		  cfg->dst_planes[3].w * dst->bpp / 8,
        	  	  cfg->dst_planes[3].h,
            	  CANVAS_ADDR_NOWRAP,
              	  CANVAS_BLKMODE_LINEAR);
        }
	}
}
static  int  
setup_display_property(src_dst_para_t *src_dst,int index)
{
#define   REG_OFFSET		0x20
	canvas_t   	canvas;
	unsigned	int  	data32;
	unsigned	int 	bpp;
	unsigned int 	block_mode[]={2,4,8,0,16,32,0,24};

	src_dst->canvas_index=index;
	canvas_read(index,&canvas);

	index=(index==OSD1_CANVAS_INDEX?0:1);
	data32=READ_MPEG_REG(VIU_OSD1_BLK0_CFG_W0+ REG_OFFSET*index);
	bpp=block_mode[(data32>>8) & 0xf];  //OSD_BLK_MODE[8..11]
	amlog_mask_level(LOG_MASK_CONFIG,LOG_LEVEL_HIGH,"osd%d : %d bpp\r\n",index,bpp);
	if(bpp < 16) return -1;

	src_dst->bpp=bpp;
	src_dst->xres=canvas.width/(bpp>>3);
	src_dst->yres=canvas.height;
	index=bpp-16 + ((data32>>2)&0xf); //color mode [2..5]
	index=bpp_type_lut[index];  //get color mode 
	src_dst->ge2d_color_index=default_ge2d_color_lut[index] ; //get matched ge2d color mode.
	
	if(src_dst->xres<=0 || src_dst->yres<=0 || src_dst->ge2d_color_index==0)
	return -2;
	
	return 0;	
	
}
int	ge2d_antiflicker_enable(ge2d_context_t *context,unsigned long enable)
{
	/*********************************************************************
	**	antiflicker used in cvbs mode, if antiflicker is enabled , it represent that we want 
	**	this feature be enabled for all ge2d work
	***********************************************************************/
	ge2d_context_t* pcontext;

	spin_lock(&ge2d_manager.event.sem_lock);
	list_for_each_entry(pcontext,&ge2d_manager.process_queue,list)
	{
		ge2dgen_antiflicker(pcontext,enable);
	}
	spin_unlock(&ge2d_manager.event.sem_lock);
	return 0;
}
int   ge2d_context_config(ge2d_context_t *context, config_para_t *ge2d_config)
{
	src_dst_para_t  src,dst,tmp;
	int type=ge2d_config->src_dst_type;
		
	amlog_mask_level(LOG_MASK_CONFIG,LOG_LEVEL_LOW," ge2d init\r\n");
	//setup src and dst  
	switch (type)
	{
		case  OSD0_OSD0:
		case  OSD0_OSD1:
		case  OSD1_OSD0:
		case ALLOC_OSD0:
    	if(0>setup_display_property(&src,OSD1_CANVAS_INDEX))
    	{
    		return -1;
    	}
		break;
		default:
		break;
	}
	switch (type)
	{
		case  OSD0_OSD1:
		case  OSD1_OSD1:
		case  OSD1_OSD0:
		case ALLOC_OSD1:
    	if(0>setup_display_property(&dst,OSD2_CANVAS_INDEX))
    	{
    		return -1;
    	}
		break;
		case ALLOC_ALLOC:
		default:
		break;
	}
	amlog_mask_level(LOG_MASK_CONFIG,LOG_LEVEL_LOW,"OSD ge2d type %d\r\n",type);
	switch (type)
	{
		case  OSD0_OSD0:
		dst=src;
		break;
		case  OSD0_OSD1:
		break;
		case  OSD1_OSD1:
		src=dst;
		break;
		case  OSD1_OSD0:
		tmp=src;
		src=dst;
		dst=tmp;
		break;
    		case ALLOC_OSD0:
		dst=src;
		build_ge2d_config(ge2d_config, &src, NULL,ALLOC_CANVAS_INDEX);
		break;
    		case ALLOC_OSD1:
		build_ge2d_config(ge2d_config, &src, NULL,ALLOC_CANVAS_INDEX);
		break;
    		case ALLOC_ALLOC:
		build_ge2d_config(ge2d_config, &src,&dst,ALLOC_CANVAS_INDEX);
		break;
	}
	if(src.bpp < 16 || dst.bpp < 16 )
	{
		amlog_mask_level(LOG_MASK_CONFIG,LOG_LEVEL_HIGH,"src dst bpp type, src=%d,dst=%d \r\n",src.bpp,dst.bpp);
	}
	
	//next will config regs
	amlog_mask_level(LOG_MASK_CONFIG,LOG_LEVEL_LOW,"ge2d xres %d yres %d : dst xres %d yres %d\r\n",src.xres,src.yres,
	dst.xres,dst.yres);
	ge2dgen_src(context,src.canvas_index, src.ge2d_color_index);
	ge2dgen_src_clip(context,
                  0, 0,src.xres, src.yres);
	ge2dgen_src2(context, dst.canvas_index, dst.ge2d_color_index);
       ge2dgen_src2_clip(context,
                            0, 0,  dst.xres, dst.yres);
	ge2dgen_const_color(context,ge2d_config->alu_const_color);	 
	ge2dgen_dst(context, dst.canvas_index,dst.ge2d_color_index);
 	ge2dgen_dst_clip(context,
                   0, 0, dst.xres, dst.yres, DST_CLIP_MODE_INSIDE);	
	return  0;
	
}
/***********************************************************************
** interface for init  create & destroy work_queue
************************************************************************/
ge2d_context_t* create_ge2d_work_queue(void)
{
	int  i;
	ge2d_queue_item_t  *p_item;
	ge2d_context_t  *ge2d_work_queue;
	int  empty;
	
	ge2d_work_queue=kzalloc(sizeof(ge2d_context_t), GFP_KERNEL);
	ge2d_work_queue->config.h_scale_coef_type=FILTER_TYPE_TRIANGLE;
	ge2d_work_queue->config.v_scale_coef_type=FILTER_TYPE_TRIANGLE;
	if(IS_ERR(ge2d_work_queue))
	{
		amlog_level(LOG_LEVEL_HIGH,"can't create work queue\r\n");
		return NULL;
	}
	INIT_LIST_HEAD(&ge2d_work_queue->work_queue);
	INIT_LIST_HEAD(&ge2d_work_queue->free_queue);
	init_waitqueue_head (&ge2d_work_queue->cmd_complete);
	spin_lock_init (&ge2d_work_queue->lock); //for process lock.
	for(i=0;i<MAX_GE2D_CMD;i++)
	{
		p_item=(ge2d_queue_item_t*)kcalloc(1,sizeof(ge2d_queue_item_t),GFP_KERNEL);
		if(IS_ERR(p_item))
		{
			amlog_level(LOG_LEVEL_HIGH,"can't request queue item memory\r\n");
			return NULL;
		}
		list_add_tail(&p_item->list, &ge2d_work_queue->free_queue) ;
	}
	
	//put this process queue  into manager queue list.
	//maybe process queue is changing .
	spin_lock(&ge2d_manager.event.sem_lock);
	empty=list_empty(&ge2d_manager.process_queue);
	list_add_tail(&ge2d_work_queue->list,&ge2d_manager.process_queue);
	spin_unlock(&ge2d_manager.event.sem_lock);
	return ge2d_work_queue; //find it 
}
int  destroy_ge2d_work_queue(ge2d_context_t* ge2d_work_queue)
{
	ge2d_queue_item_t    	*pitem,*tmp;
	struct list_head  		*head;
	int empty;
	if (ge2d_work_queue) {
		//first detatch  it from the process queue,then delete it .	
		//maybe process queue is changing .so we lock it.
		spin_lock(&ge2d_manager.event.sem_lock);
		list_del(&ge2d_work_queue->list);
		empty=list_empty(&ge2d_manager.process_queue);
		spin_unlock(&ge2d_manager.event.sem_lock);
		if((ge2d_manager.current_wq==ge2d_work_queue)&&(ge2d_manager.ge2d_state== GE2D_STATE_RUNNING))
		{
			ge2d_manager.ge2d_state=GE2D_STATE_REMOVING_WQ;
			wait_for_completion(&ge2d_manager.event.process_complete);
			ge2d_manager.last_wq=NULL;  //condition so complex ,simplify it .
		}//else we can delete it safely.
		
		head=&ge2d_work_queue->work_queue;
		list_for_each_entry_safe(pitem,tmp,head,list){
			if(pitem)  
			{
				list_del(&pitem->list );
				kfree(pitem);
			}
		}
		head=&ge2d_work_queue->free_queue;	
		list_for_each_entry_safe(pitem,tmp,head,list){
			if(pitem)  
			{
				list_del(&pitem->list );
				kfree(pitem);
			}
		}
		
     		kfree(ge2d_work_queue);
        	ge2d_work_queue=NULL;
		return 0;
    	}
	
    	return  -1;	
}
/***********************************************************************
** interface for init and deinit section
************************************************************************/
int ge2d_wq_init(void)
{
   	ge2d_gen_t           ge2d_gen_cfg;
	

	amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"enter %s line %d\r\n",__func__,__LINE__)	;    
	
    	if ((ge2d_manager.irq_num=request_irq(GE2D_IRQ_NO, ge2d_wq_handle , IRQF_SHARED,"ge2d irq", (void *)&ge2d_manager))<0)
   	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"ge2d request irq error\r\n")	;
		return -1;
	}
	//prepare bottom half		
	
	spin_lock_init(&ge2d_manager.event.sem_lock);
	init_MUTEX (&ge2d_manager.event.cmd_in_sem); 
	init_waitqueue_head (&ge2d_manager.event.cmd_complete);
	init_completion(&ge2d_manager.event.process_complete);
	INIT_LIST_HEAD(&ge2d_manager.process_queue);
	ge2d_manager.last_wq=NULL;
	ge2d_manager.ge2d_thread=NULL;
    	ge2d_soft_rst();
    	ge2d_gen_cfg.interrupt_ctrl = 0x02;
    	ge2d_gen_cfg.dp_on_cnt       = 150;
    	ge2d_gen_cfg.dp_off_cnt      = 100;
    	ge2d_gen_cfg.dp_onoff_mode   = 0;
    	ge2d_gen_cfg.vfmt_onoff_en   = 0;
    	ge2d_set_gen(&ge2d_gen_cfg);
	if(ge2d_start_monitor())
 	{
 		amlog_level(LOG_LEVEL_HIGH,"ge2d create thread error\r\n");	
		return -1;
 	}	
	return 0;
}
int   ge2d_setup(void)
{
	// do init work for ge2d.
	if (ge2d_wq_init())
      	{
      		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"ge2d work queue init error \r\n");	
		return -1;	
      	}
 	return  0;
}
EXPORT_SYMBOL(ge2d_setup);
int   ge2d_deinit( void )
{
	ge2d_stop_monitor();
	amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"deinit ge2d device \r\n") ;
	if (ge2d_manager.irq_num >= 0) {
      		free_irq(GE2D_IRQ_NO,&ge2d_manager);
       	 ge2d_manager.irq_num= -1;
    	}
	return  0;
}
EXPORT_SYMBOL(ge2d_deinit);


