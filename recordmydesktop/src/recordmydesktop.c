/*********************************************************************************
*                             recordMyDesktop                                    *
**********************************************************************************
*                                                                                *
*             Copyright (C) 2006  John Varouhakis                                *
*                                                                                *
*                                                                                *
*    This program is free software; you can redistribute it and/or modify        *
*    it under the terms of the GNU General Public License as published by        *
*    the Free Software Foundation; either version 2 of the License, or           *
*    (at your option) any later version.                                         *
*                                                                                *
*    This program is distributed in the hope that it will be useful,             *
*    but WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the               *
*    GNU General Public License for more details.                                *
*                                                                                *
*    You should have received a copy of the GNU General Public License           *
*    along with this program; if not, write to the Free Software                 *
*    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA   *
*                                                                                *
*                                                                                *
*                                                                                *
*    For further information contact me at biocrasher@gmail.com                  *
**********************************************************************************/


#include <recordmydesktop.h>


int main(int argc,char **argv){
    ProgData pdata;

    if(XInitThreads ()==0){
        fprintf(stderr,"Couldn't initialize thread support!\n");
        exit(1);
    }
    DEFAULT_ARGS(&pdata.args);
    if(ParseArgs(argc,argv,&pdata.args)){
        exit(1);
    }
    pdata.dpy = XOpenDisplay(pdata.args.display);

    if (pdata.dpy == NULL) {
        fprintf(stderr, "Cannot connect to X server %s\n",pdata.args.display);
        exit(1);
    }
    else{
        EncData enc_data;
        pthread_t   poll_damage_t,
                    image_capture_t,
                    image_encode_t,
                    sound_capture_t,
                    sound_encode_t,
                    flush_to_ogg_t;
        XShmSegmentInfo shminfo;

        QUERY_DISPLAY_SPECS(pdata.dpy,&pdata.specs);
        if(pdata.specs.depth!=24){
            fprintf(stderr,"Only 24bpp color depth mode is currently supported.\n");
            exit(1);
        }
        if(SetBRWindow(pdata.dpy,&pdata.brwin,&pdata.specs,&pdata.args))
            exit(1);
        if(QueryExtensions(pdata.dpy,&pdata.args,&pdata.damage_event, &pdata.damage_error))
            exit(1);

        //init data

        if(!pdata.args.scshot){
            fprintf(stderr,"Initializing...\n");
    
            if(pdata.args.have_dummy_cursor){
                pdata.dummy_pointer=MakeDummyPointer(&pdata.specs,16,pdata.args.cursor_color,0,&pdata.npxl);
                pdata.dummy_p_size=16;
            }
        }
        if((pdata.args.noshared)||(pdata.args.scshot))
            pdata.datamain=(char *)malloc(pdata.brwin.nbytes);
        if(!pdata.args.scshot){
            if(pdata.args.noshared)
                pdata.datatemp=(char *)malloc(pdata.brwin.nbytes);
            pdata.rect_root[0]=pdata.rect_root[1]=NULL;
            pthread_mutex_init(&pdata.list_mutex[0],NULL);
            pthread_mutex_init(&pdata.list_mutex[1],NULL);
            pthread_mutex_init(&pdata.sound_buffer_mutex,NULL);
            pthread_mutex_init(&pdata.yuv_mutex,NULL);

            pthread_cond_init(&pdata.time_cond,NULL);
            pthread_cond_init(&pdata.pause_cond,NULL);
            pthread_cond_init(&pdata.image_buffer_ready,NULL);
            pthread_cond_init(&pdata.sound_buffer_ready,NULL);
            pthread_cond_init(&pdata.sound_data_read,NULL);
            pdata.list_selector=Paused=Aborted=avd=0;
            pdata.running=1;       
            time_cond=&pdata.time_cond;
            pause_cond=&pdata.pause_cond;
            Running=&pdata.running;
        }
        if((pdata.args.noshared)||(pdata.args.scshot)){
            pdata.image=XCreateImage(pdata.dpy, pdata.specs.visual, pdata.specs.depth, ZPixmap, 0,pdata.datamain,pdata.brwin.rgeom.width,
                        pdata.brwin.rgeom.height, 8, 0);
            XInitImage(pdata.image);
            GetZPixmap(pdata.dpy,pdata.specs.root,pdata.image->data,pdata.brwin.rgeom.x,pdata.brwin.rgeom.y,
                        pdata.brwin.rgeom.width,pdata.brwin.rgeom.height);
        }
        else{
            pdata.image=XShmCreateImage (pdata.dpy,pdata.specs.visual,pdata.specs.depth,ZPixmap,pdata.datamain, 
                         &shminfo, pdata.brwin.rgeom.width,pdata.brwin.rgeom.height);
            shminfo.shmid = shmget (IPC_PRIVATE,
                                    pdata.image->bytes_per_line * pdata.image->height,
                                    IPC_CREAT|0777);
            shminfo.shmaddr = pdata.image->data = shmat (shminfo.shmid, 0, 0);
            shminfo.readOnly = False;
            if(!XShmAttach(pdata.dpy,&shminfo)){
                fprintf(stderr,"Failed to attach shared memory to proccess.\n");
                exit(1);
            }
            XShmGetImage(pdata.dpy,pdata.specs.root,pdata.image,0,0,AllPlanes);

        }
        if(pdata.args.scshot){
            if(pdata.args.delay>0){
                fprintf(stderr,"Will sleep for %d seconds now.\n",pdata.args.delay);
                sleep(pdata.args.delay);
            }
            //get a new screenshot
            GetZPixmap(pdata.dpy,pdata.specs.root,pdata.image->data,pdata.brwin.rgeom.x,pdata.brwin.rgeom.y,
                    pdata.brwin.rgeom.width,pdata.brwin.rgeom.height);
            ZPixmapToBMP(pdata.image,&pdata.brwin,((!strcmp(pdata.args.filename,"out.ogg"))?"rmdout.bmp":pdata.args.filename),pdata.brwin.nbytes,pdata.args.scale_shot);
            fprintf(stderr,"done!\n");
            exit(0);
        }
        if(!pdata.args.nosound)
            pdata.sound_handle=OpenDev(pdata.args.device,pdata.args.channels,&pdata.args.frequency,&pdata.periodsize,            &pdata.periodtime,&pdata.hard_pause);
        if(pdata.sound_handle==NULL){
            fprintf(stderr,"Error while opening/configuring soundcard %s\nProcceeding with no sound\n",pdata.args.device);
            pdata.args.nosound=1;
        }
        InitEncoder(&pdata,&enc_data);
        XImageToYUV(pdata.image,&pdata.enc_data->yuv);

        pdata.frametime=(1000000)/pdata.args.fps;

        if(pdata.args.delay>0){
            fprintf(stderr,"Will sleep for %d seconds now.\n",pdata.args.delay);
            sleep(pdata.args.delay);
        }

        /*start threads*/
        if(!pdata.args.full_shots)
            pthread_create(&poll_damage_t,NULL,PollDamage,(void *)&pdata);
        pthread_create(&image_capture_t,NULL,GetFrame,(void *)&pdata);
        pthread_create(&image_encode_t,NULL,EncodeImageBuffer,(void *)&pdata);
        if(!pdata.args.nosound){
            pthread_create(&sound_capture_t,NULL,CaptureSound,(void *)&pdata);
            pthread_create(&sound_encode_t,NULL,EncodeSoundBuffer,(void *)&pdata);
        }
        pthread_create(&flush_to_ogg_t,NULL,FlushToOgg,(void *)&pdata);
        

        RegisterCallbacks(&pdata.args);
        fprintf(stderr,"Capturing!\n");

        //wait all threads to finish
        
        pthread_join(image_capture_t,NULL);
        fprintf(stderr,"Shutting down.");
        pthread_join(image_encode_t,NULL);
        fprintf(stderr,".");
        if(!pdata.args.nosound){
            pthread_join(sound_capture_t,NULL);
            fprintf(stderr,".");
            pthread_join(sound_encode_t,NULL);
            fprintf(stderr,".");
        }
        else
            fprintf(stderr,"..");
        pthread_join(flush_to_ogg_t,NULL);
        fprintf(stderr,".");
        if(!pdata.args.full_shots)
            pthread_join(poll_damage_t,NULL);
        fprintf(stderr,".");
        if(!(pdata.args.noshared)){
            XShmDetach (pdata.dpy, &shminfo);
//             XDestroyImage (pdata.image);
            shmdt (&shminfo.shmaddr);
            shmctl (shminfo.shmid, IPC_RMID, 0);
        }
        fprintf(stderr,"\n");
        XCloseDisplay(pdata.dpy);
        if(Aborted){
            if(remove(pdata.args.filename)){
                perror("Error while removing file:\n");
                return 1;
            }
            else{
                fprintf(stderr,"SIGABRT received,file %s removed\n",pdata.args.filename);
                return 0;
            }
        }
        else
            fprintf(stderr,"Goodbye!\n");
    }
    return 0;
}















