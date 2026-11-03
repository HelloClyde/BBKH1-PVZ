#include <xmp.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static unsigned long long energy(const struct xmp_frame_info *frame) {
    const int16_t *p=(const int16_t*)frame->buffer;
    int i,n=frame->buffer_size/2;
    unsigned long long result=0;
    for (i=0;i<n;i++) result+=(unsigned long long)((int)p[i]*p[i]);
    return result;
}
int main(int argc,char **argv) {
    /* Actual tune offsets from the pinned original Music.cpp. */
    const int orders[]={0x98,0,0x30,0x5c,0x5e,0x7d,0xb8,0x7a,0xdd,0xb1,0xa6,0xd4,76,77};
    xmp_context original,compact;
    struct xmp_module_info module;
    struct xmp_frame_info old_frame,new_frame;
    int i,j,frames,first_pos,first_row,old_result,new_result,muted_peak=0;
    unsigned long long total_energy;
    if (argc!=3) return 2;
    original=xmp_create_context();compact=xmp_create_context();
    if (!original || !compact) return 3;
    old_result=xmp_load_module(original,argv[1]);new_result=xmp_load_module(compact,argv[2]);
    if (old_result || new_result) {fprintf(stderr,"module load failed: original=%d compact=%d\n",old_result,new_result);return 4;}
    xmp_get_module_info(compact,&module);
    if (module.mod->chn!=30 || module.mod->len!=236 || module.mod->pat<200) {
        fprintf(stderr,"module dimensions: channels=%d orders=%d patterns=%d\n",module.mod->chn,module.mod->len,module.mod->pat);return 5;
    }
    if (xmp_start_player(original,16000,XMP_FORMAT_MONO) || xmp_start_player(compact,16000,XMP_FORMAT_MONO)) return 6;
    puts("order,first_pos,first_row,last_pos,last_row,frames,pcm_energy");
    for (i=0;i<(int)(sizeof(orders)/sizeof(orders[0]));i++) {
        xmp_restart_module(original);xmp_restart_module(compact);
        /* libxmp represents the pre-first-row position for order 0 as -1. */
        if (xmp_set_position(original,orders[i])<-1 || xmp_set_position(compact,orders[i])<-1) return 7;
        if (xmp_set_row(original,0)<0 || xmp_set_row(compact,0)<0) return 7;
        total_energy=0;first_pos=first_row=-1;frames=0;
        for (j=0;j<300;j++) {
            old_result=xmp_play_frame(original);new_result=xmp_play_frame(compact);
            if (old_result!=new_result) return 8;
            if (old_result) break;
            xmp_get_frame_info(original,&old_frame);xmp_get_frame_info(compact,&new_frame);
            if (old_frame.pos!=new_frame.pos || old_frame.row!=new_frame.row || old_frame.pattern!=new_frame.pattern ||
                old_frame.speed!=new_frame.speed || old_frame.bpm!=new_frame.bpm || old_frame.buffer_size!=new_frame.buffer_size) return 9;
            if (!frames) {first_pos=new_frame.pos;first_row=new_frame.row;}
            total_energy+=energy(&new_frame);frames++;
        }
        if (!frames || first_pos!=orders[i]) return 10;
        printf("%d,%d,%d,%d,%d,%d,%llu\n",orders[i],first_pos,first_row,new_frame.pos,new_frame.row,frames,total_energy);
    }
    /* Muting every real tracker channel must silence the integer mixer, while
     * restoring per-channel volume must bring the same running tune back. */
    xmp_end_player(compact);
    if (xmp_start_player(compact,16000,XMP_FORMAT_MONO)) return 19;
    xmp_set_position(compact,0x98);xmp_set_row(compact,0);
    if (xmp_play_frame(compact)) return 19;
    for (i=0;i<30;i++) {if (xmp_channel_vol(compact,i,0)<0) return 11;xmp_channel_mute(compact,i,1);}
    total_energy=0;
    for (i=0;i<50;i++) {
        if (xmp_play_frame(compact)) return 12;
        xmp_get_frame_info(compact,&new_frame);
        if (i>=10) {
            const int16_t *p=(const int16_t*)new_frame.buffer;
            total_energy+=energy(&new_frame);
            for (j=0;j<new_frame.buffer_size/2;j++) {
                int v=p[j]<0?-(int)p[j]:(int)p[j];if (v>muted_peak) muted_peak=v;
            }
        }
    }
    if (muted_peak>2) return 13;
    for (i=0;i<30;i++) {if (xmp_channel_vol(compact,i,100)<0) return 14;xmp_channel_mute(compact,i,0);}
    total_energy=0;
    for (i=0;i<100;i++) {
        if (xmp_play_frame(compact)) return 15;
        xmp_get_frame_info(compact,&new_frame);total_energy+=energy(&new_frame);
    }
    if (!total_energy) return 16;
    /* Packed order stores the exact row; verify the real resume API. */
    xmp_set_position(compact,0x98);xmp_set_row(compact,17);
    if (xmp_play_frame(compact)) return 17;
    xmp_get_frame_info(compact,&new_frame);
    if (new_frame.pos!=0x98 || new_frame.row!=17) return 18;
    printf("controls,30 channels,muted_peak=%d,restored_energy=%llu,resume=%d:%d\n",muted_peak,total_energy,new_frame.pos,new_frame.row);
    xmp_end_player(original);xmp_end_player(compact);
    xmp_release_module(original);xmp_release_module(compact);
    xmp_free_context(original);xmp_free_context(compact);
    return 0;
}
