
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                            main.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Xiao hong, 2016
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"
#include "keyboard.h"


/*****************************************************************************
 *                               kernel_main
 *****************************************************************************/
/**
 * jmp from kernel.asm::_start. 
 * 
 *****************************************************************************/
#define fileSize 8
char location[128] = "User";
char curFile[128]="User";
char fileNames[20][128]={"User"};
int fileCount=0;
char password[10]="123";
char curUser[10]="sjw";


int flag=0;
int FAT[10][fileSize]={0};






void sleep(int pauseTime){
	int i = 0;
	for(i=0;i<pauseTime*1000000;i++){
;
}
}






/*num2*/


PUBLIC int kernel_main()
{
	disp_str("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"
		 "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");

	int i, j, eflags, prio;
        u8  rpl;
        u8  priv; /* privilege */

	struct task * t;
	struct proc * p = proc_table;

	char * stk = task_stack + STACK_SIZE_TOTAL;

	for (i = 0; i < NR_TASKS + NR_PROCS; i++,p++,t++) {
		if (i >= NR_TASKS + NR_NATIVE_PROCS) {
			p->p_flags = FREE_SLOT;
			continue;
		}

	        if (i < NR_TASKS) {     /* TASK */
                        t	= task_table + i;
                        priv	= PRIVILEGE_TASK;
                        rpl     = RPL_TASK;
                        eflags  = 0x1202;/* IF=1, IOPL=1, bit 2 is always 1 */
			prio    = 15;
                }
                else {                  /* USER PROC */
                        t	= user_proc_table + (i - NR_TASKS);
                        priv	= PRIVILEGE_USER;
                        rpl     = RPL_USER;
                        eflags  = 0x202;	/* IF=1, bit 2 is always 1 */
			prio    = 5;
                }

		strcpy(p->name, t->name);	/* name of the process */
		p->p_parent = NO_TASK;

		if (strcmp(t->name, "INIT") != 0) {
			p->ldts[INDEX_LDT_C]  = gdt[SELECTOR_KERNEL_CS >> 3];
			p->ldts[INDEX_LDT_RW] = gdt[SELECTOR_KERNEL_DS >> 3];

			/* change the DPLs */
			p->ldts[INDEX_LDT_C].attr1  = DA_C   | priv << 5;
			p->ldts[INDEX_LDT_RW].attr1 = DA_DRW | priv << 5;
		}
		else {		/* INIT process */
			unsigned int k_base;
			unsigned int k_limit;
			int ret = get_kernel_map(&k_base, &k_limit);
			assert(ret == 0);
			init_desc(&p->ldts[INDEX_LDT_C],
				  0, /* bytes before the entry point
				      * are useless (wasted) for the
				      * INIT process, doesn't matter
				      */
				  (k_base + k_limit) >> LIMIT_4K_SHIFT,
				  DA_32 | DA_LIMIT_4K | DA_C | priv << 5);

			init_desc(&p->ldts[INDEX_LDT_RW],
				  0, /* bytes before the entry point
				      * are useless (wasted) for the
				      * INIT process, doesn't matter
				      */
				  (k_base + k_limit) >> LIMIT_4K_SHIFT,
				  DA_32 | DA_LIMIT_4K | DA_DRW | priv << 5);
		}

		p->regs.cs = INDEX_LDT_C << 3 |	SA_TIL | rpl;
		p->regs.ds =
			p->regs.es =
			p->regs.fs =
			p->regs.ss = INDEX_LDT_RW << 3 | SA_TIL | rpl;
		p->regs.gs = (SELECTOR_KERNEL_GS & SA_RPL_MASK) | rpl;
		p->regs.eip	= (u32)t->initial_eip;
		p->regs.esp	= (u32)stk;
		p->regs.eflags	= eflags;

		p->ticks = p->priority = prio;

		p->p_flags = 0;
		p->p_msg = 0;
		p->p_recvfrom = NO_TASK;
		p->p_sendto = NO_TASK;
		p->has_int_msg = 0;
		p->q_sending = 0;
		p->next_sending = 0;

		for (j = 0; j < NR_FILES; j++)
			p->filp[j] = 0;

		stk -= t->stacksize;
	}

	k_reenter = 0;
	ticks = 0;

	p_proc_ready	= proc_table;

	init_clock();
        init_keyboard();

	restart();

	while(1){}
}





/*****************************************************************************
 *                                get_ticks
 *****************************************************************************/
PUBLIC int get_ticks()
{
	MESSAGE msg;
	reset_msg(&msg);
	msg.type = GET_TICKS;
	send_recv(BOTH, TASK_SYS, &msg);
	return msg.RETVAL;
}


/**
 * @struct posix_tar_header
 * Borrowed from GNU `tar'
 */
struct posix_tar_header
{				/* byte offset */
	char name[100];		/*   0 */
	char mode[8];		/* 100 */
	char uid[8];		/* 108 */
	char gid[8];		/* 116 */
	char size[12];		/* 124 */
	char mtime[12];		/* 136 */
	char chksum[8];		/* 148 */
	char typeflag;		/* 156 */
	char linkname[100];	/* 157 */
	char magic[6];		/* 257 */
	char version[2];	/* 263 */
	char uname[32];		/* 265 */
	char gname[32];		/* 297 */
	char devmajor[8];	/* 329 */
	char devminor[8];	/* 337 */
	char prefix[155];	/* 345 */
	/* 500 */
};

/*****************************************************************************
 *                                untar
 *****************************************************************************/
/**
 * Extract the tar file and store them.
 * 
 * @param filename The tar file.
 *****************************************************************************/
void untar(const char * filename)
{
	printf("[extract `%s' ", filename);
	int fd = open(filename, O_RDWR);
	assert(fd != -1);

	char buf[SECTOR_SIZE * 16];
	int chunk = sizeof(buf);

	while (1) {
		read(fd, buf, SECTOR_SIZE);
		if (buf[0] == 0)
			break;

		struct posix_tar_header * phdr = (struct posix_tar_header *)buf;

		/* calculate the file size */
		char * p = phdr->size;
		int f_len = 0;
		while (*p)
			f_len = (f_len * 8) + (*p++ - '0'); /* octal */

		int bytes_left = f_len;
		int fdout = open(phdr->name, O_CREAT | O_RDWR);
		if (fdout == -1) {
			printf("    failed to extract file: %s\n", phdr->name);
			printf(" aborted]");
			return;
		}
		printf("    %s (%d bytes) ", phdr->name, f_len);
		while (bytes_left) {
			int iobytes = min(chunk, bytes_left);
			read(fd, buf,
			     ((iobytes - 1) / SECTOR_SIZE + 1) * SECTOR_SIZE);
			write(fdout, buf, iobytes);
			bytes_left -= iobytes;
		}
		close(fdout);
	}

	close(fd);

	printf(" done]\n");
}



/*****************************************************************************
 *                                Init
 *****************************************************************************/
/**
 * The hen.
 * 
 *****************************************************************************/
void Init()
{
	int fd_stdin  = open("/dev_tty0", O_RDWR);
	assert(fd_stdin  == 0);
	int fd_stdout = open("/dev_tty0", O_RDWR);
	assert(fd_stdout == 1);

	//printf("Init() is running ...\n");

	/* extract `cmd.tar' */
	untar("/cmd.tar");
			

	char * tty_list[] = {"/dev_tty0"};

	int i;
	for (i = 0; i < sizeof(tty_list) / sizeof(tty_list[0]); i++) {
		int pid = fork();
		if (pid != 0) { /* parent process */
		}
		else {	/* child process */
			close(fd_stdin);
			close(fd_stdout);
			
			shabby_shell(tty_list[i]);
			assert(0);
		}
	}

	while (1) {
		int s;
		int child = wait(&s);
		printf("child (%d) exited with status: %d.\n", child, s);
	}

	assert(0);
}


/*======================================================================*
                               TestA
 *======================================================================*/
void TestA()
{
	while(1)
	{
		//printl("%f\n",get_ticks());
	}
}

/*======================================================================*
                               TestB
 *======================================================================*/
void TestB()
{
	for(;;);
}

/*======================================================================*
                               TestB
 *======================================================================*/
void TestC()
{
	for(;;);
}

/*****************************************************************************
 *                                panic
 *****************************************************************************/
PUBLIC void panic(const char *fmt, ...)
{
	int i;
	char buf[256];

	/* 4 is the size of fmt in the stack */
	va_list arg = (va_list)((char*)&fmt + 4);

	i = vsprintf(buf, fmt, arg);

	printl("%c !!panic!! %s", MAG_CH_PANIC, buf);

	/* should never arrive here */
	__asm__ __volatile__("ud2");
}

void hello(){
	clear();
	char *hello = "\n\n"
"                                                                       \n"
"        ****     ****   **   ****     **   **       *******    ********\n"
"       /**/**   **/**  /**  /**/**   /**  /**      **/////**  **////// \n"
"       /**//** ** /**  /**  /**//**  /**  /**     **     //**/**       \n"
"       /** //***  /**  /**  /** //** /**  /**    /**      /**/*********\n"
"       /**  //*   /**  /**  /**  //**/**  /**    /**      /**////////**\n"
"       /**   /    /**  /**  /**   //****  /**    //**     **        /**\n"
"       /**        /**  /**  /**    //***  /**     //*******   ******** \n"
"       //         //   //   //      ///   //       ///////   ////////  \n"
"                                                                       \n"
"                                                                       \n"
"                                                                       \n"
"                                                                       \n"
"                                                                       \n"
"                                                                       \n"
"*****************************************************************************\n"
"****                     welcome to our os                               ****\n"
"****                            made by:       Shen Jinwei   1552756     ****\n"
"****                                           Chen    hui   1552698     ****\n"
"****                                           Cai  Xuyang   1552734     ****\n"
"****                                           2017  8.15                ****\n"
"*****************************************************************************\n";
	int ch = 0;
	for (ch = 0; ch <= strlen(hello); ch++){
		printl("%c", hello[ch]);
		milli_delay(20);
	}


	

}

/*****************************************************************************
 *                                xiaohong_shell
 *****************************************************************************/
/**
 * A very very powerful shell.
 * 
 * @param tty_name  TTY file name.
 *****************************************************************************/
void shabby_shell(const char * tty_name)
{

	
	int fd_stdin  = open(tty_name, O_RDWR);
	assert(fd_stdin  == 0);
	int fd_stdout = open(tty_name, O_RDWR);
	assert(fd_stdout == 1);

	char rdbuf[128];
	char cmd[128];
    	char arg1[128];
    	char arg2[128];
    	char buf[1024];
	int j = 0;

	//colorful();
	hello();
	//welcome();
	printf("press any key to start:\n");
	int r = read(0, rdbuf, 128);
	printf("press any key to start:\n");
	help();
	r = read(0, rdbuf, 128);
	initFs();

	while (1) {

		clearArr(rdbuf, 128);
        	clearArr(cmd, 128);
        	clearArr(arg1, 128);
        	clearArr(arg2, 128);
        	clearArr(buf, 1024);
		
		printf("%s $ ", location);
		int r = read(0, rdbuf, 70);
		rdbuf[r] = 0;
	

		int argc = 0;
		char * argv[PROC_ORIGIN_STACK];
		char * p = rdbuf;
		char * s;
		int word = 0;
		char ch;
		do {
			ch = *p;
			if (*p != ' ' && *p != 0 && !word) {
				s = p;
				word = 1;
			}
			if ((*p == ' ' || *p == 0) && word) {
				word = 0;
				argv[argc++] = s;
				*p = 0;
			}
			p++;
		} while(ch);
		argv[argc] = 0;

		int fd = open(argv[0], O_RDWR);
		if (fd == -1) {
			if (rdbuf[0]) {
				int i = 0, j = 0;
				/* get command */
				while (rdbuf[i] != ' ' && rdbuf[i] != 0)
				{
					cmd[i] = rdbuf[i];
					i++;
				}
				i++;
				/* get arg1 */
				while(rdbuf[i] != ' ' && rdbuf[i] != 0)
        			{
            				arg1[j] = rdbuf[i];
            				i++;
            				j++;
        			}
        			i++;
        			j = 0;
				/* get arg2 */
       				while(rdbuf[i] != ' ' && rdbuf[i] != 0)
        			{
            				arg2[j] = rdbuf[i];
            				i++;
            				j++;
        			}
				if(strcmp(cmd,"login")==0)
				  {
				    login(arg1,arg2);
				  }
				 else if(strcmp(cmd,"welcome")==0)
				 		{
				 			welcome();
				 		}
				else if(strcmp(cmd,"newLogin")==0)
				  {
				    newLogin();
				  }
				  else if (strcmp(cmd, "rrd") == 0)
				  {
				    rrd(arg1);
				  }
				  else if(strcmp(cmd,"clear")==0)
				  {
				  	clear();
				  }
				  else if(strcmp(cmd,"help")==0)
				  {
				  	help();
				  }

				else if(hasLogined()==0)
				continue;
				  else if(strcmp(cmd,"rct")==0)
				  {
				  	realCreate(arg1);
				  }
				else if(strcmp(cmd,"proc")==0)
				{
					showProcess();
				}
				else if (strcmp(cmd, "rdt") == 0)
				  {
				    unlink(arg1);
				  }			   
				else if (strcmp(cmd, "save") == 0)
				  {
				    save();
				  }		
				else if (strcmp(cmd, "rrd") == 0)
				  {
				    rrd(arg1);
				  }
				else if (strcmp(cmd, "rwd") == 0)
				  {
				    rwd(arg1);
				  }
				else if(strcmp(cmd,"ct")==0)
				  {
				     createFile(arg1);
				  }
				else if(strcmp(cmd,"color")==0)
				  {
				  	colorful();
				  	}
				else if(strcmp(cmd,"edit")==0)
				  {
				    editFile(arg1);
				  }
				else if(strcmp(cmd,"read")==0)
				  {
				    readFile(arg1); 
				  }
		       
				else if(strcmp(cmd,"ls")==0)
				{
				  	lsFile();	
				}
				else if(strcmp(cmd,"cd")==0)
				{
					cdFile(arg1);
				}
			    else if(strcmp(cmd,"delete")==0)
				{
					deleteFile(arg1);
				}
				else if(strcmp(cmd,"red")==0)
				{
					realEdit(arg1,arg2);
				}
				else if(strcmp(cmd,"initFLAT")==0)
				{
					for(i=0;i<10;i++)
						for(j=0;j<fileSize;j++)
							FAT[i][j]=0;
				}
				
				else if(strcmp(cmd,"testArr")==0)
				  {
				   
				    printf("location:%s\n",location);
				    printf("fileNames:\n");
			        
				    for(i=0;i<20;i++)
				      printf("%s | ",fileNames[i]);
				    printf("FAT:\n");

				    for(i=0;i<10;i++)
				      {
					for(j=0;j<fileSize;j++)
					  printf("%d",FAT[i][j]);
					printf("\n");
				      }
				    printf("current file nums %d\n",fileCount);
				    printf("current User: %s\n",curUser);		    
	     		  }
	     		  else
	     		  {
	     		  	printf("unknow command\n");
	     		  }
				
				
			}
		}
		else {
			close(fd);
			int pid = fork();
			if (pid != 0) { /* parent */
				int s;
				wait(&s);
			}
			else {	/* child */
				execv(argv[0], argv);
			}
		}
	}

	close(1);
	close(0);
}

/* Tools */

	/* Init Arr */
void clearArr(char *arr, int length)
{
    int i;
    for (i = 0; i < length; i++)
        arr[i] = 0;
}

void login(char * arg1,char *arg2 )
{
  if(strcmp(arg1,curUser)==0&&strcmp(arg2,password)==0)
    {
       flag=1;
       printf("Welcome %s!\n",arg1);
       return;
    }
  printf("Wrong Password!\n");
  return;
}

int hasLogined()
{
  if(flag==0)
    {
      printf("Login First!\n");
      return 0;
    }
  return 1;
}

void newLogin()
{
  char temp1[10];
  char temp2[10];
  clearArr(temp1,10);
  clearArr(temp2,10);
  printf("enter your username:\n");
  read(0,temp1,10);
  
  printf("enter your password:\n");
  read(0,temp2,10);
  
  printf("Welcome %s!\n",temp1);

  clearAll(temp1,temp2);
}





void clearAll(char * temp1,char* temp2)
{
  int i=0,j=0;

 clearArr(location,128);
 clearArr(curFile,128);
 fileCount=0;
 strcpy(curUser,temp1);
 strcpy(password,temp2);
 strcpy(curFile,"User");
 strcpy(location,"User");
 flag=1;

 for( i=1;i<20;i++)
   {
     if(strlen(fileNames[i])!=0)
       {
       unlink(fileNames[i]);
       }
     clearArr(fileNames[i],128);
   }
   

 for(i=0;i<10;i++)
   for(j=0;j<fileSize;j++)
     FAT[i][j]=0;
 save();
 return;
}

void createAll()
{
  int fd = -1;

  fd = open("User",O_CREAT);
  assert(fd!=-1);
  close(fd);
   
  fd = open("NoUse",O_CREAT);
  assert(fd!=-1);
  close(fd);
  
 fd = open("Password",O_CREAT);
  assert(fd!=-1);
  close(fd);

 fd = open("FAT",O_CREAT);
  assert(fd!=-1);
  close(fd);


 fd = open("FileNames",O_CREAT);
  assert(fd!=-1);
  close(fd);


}

void realCreate(char *fileName)
{
	int fd = open(fileName,O_CREAT);
	close(fd);
}

void realEdit(char *fileName,char* content)
{
	int fd = open(fileName,O_RDWR);
	write(fd,content,strlen(content));
	close(fd);
}

void save()
{
  	char temp1[10][fileSize]={};
	char temp2[1024];
	char temp4[10];
	int i=0,j=0,fd=-1;
	
	/* save User */
	fd =open("User",O_RDWR);
	assert(fd!=-1);
	write(fd,curUser,10);
	close(fd);


	/* save password */

	fd =open("Password",O_RDWR);
	assert(fd!=-1);
	write(fd,password,10);
	close(fd);

		
	/* save FAT */
       	for(i=0;i<10;i++)
	  {
	    for(j=0;j<fileSize;j++)
	      temp1[i][j]= FAT[i][j];
	  }

	fd=open("FAT",O_RDWR);
      	for(i=0;i<10;i++)
	   write(fd,temp1[i],fileSize);
	close(fd);

	/* save fileNames */
	fd = open("FileNames",O_RDWR);
        assert(fd!=-1);
	clearArr(temp2,1024);
	write(fd,temp2,1024);
	close(fd);

	fd = open("FileNames",O_RDWR);
	int num = fileCount;
	for(i=1;num!=0;i++)
	  {
	    if(strlen(fileNames[i])==0)
	      {
		strcat(temp2,"empty");
		strcat(temp2,"#");
	      }
	    else
	      { 
		strcat(temp2,fileNames[i]);
		strcat(temp2,"#");
		num--;
	      }	   
	  }
	
	write(fd,temp2,strlen(temp2));
	close(fd);
	  	
}

void rrd(char* fileName)
{
  int fd=-1,r=0;
  char temp[128];

  clearArr(temp,128);
  fd=open(fileName,O_RDWR);
  assert(fd!=-1);
  r = read(fd,temp,10);
  close(fd);
  
  printf("content in %s %d\n",temp,r);
}

void rwd(char * fileName)
{
   int fd=-1,r=0;
   char temp[128];

   clearArr(temp,128);
   read(0,temp,128);
   fd=open(fileName,O_RDWR | O_CREAT);
   assert(fd!=-1);
   r = write(fd,temp,10);
   close(fd);
}


/* Init FS */
void initFs()
{
  int fd=-1,i=0,r=0,j=0;
        char temp[128]={};
	
	/* init User */
	clearArr(curUser,10);
	clearArr(temp,128);
	fd=open("User",O_RDWR | O_CREAT);
	r = read(fd,temp,128);
	close(fd);

	for(i=0;i<10;i++)
	{
	  curUser[i]=temp[i];	
	}

	/* init password */
	clearArr(password,10);
	clearArr(temp,128);
	fd=open("Password",O_RDWR | O_CREAT);
	r = read(fd,temp,128);


	close(fd);

	for(i=0;i<10;i++)
	{
	  password[i]=temp[i];
	}
	



	/* init FAG */
	char temp2[10*fileSize]={};
	for(i=0;i<10;i++)
	  {
	    clearArr(FAT[i],fileSize);
	  }
       
	fd=open("FAT",O_RDWR | O_CREAT);
	r = read(fd,temp2,fileSize*10);
	close(fd);

	for(i=0;i<10;i++)
	  {
	    for(j=0;j<fileSize;j++)
	       FAT[i][j]= temp2[i*fileSize+j];
	  }

	
	/* init FileNames */
	char temp3[1024];
	clearArr(temp3,1024);
	fd = open("FileNames",O_RDWR | O_CREAT);
    assert(fd!=-1);
	read(fd,temp3,1024);
	close(fd);
	i=0,j=0;
	fileCount=0;
	int  num=0;
	while(i<strlen(temp3))
	  {
	    while(temp3[i]!='#')
	      {
		fileNames[num+1][j]=temp3[i];
		j++;
		i++;
		//sleep(5);
	      }
	    if(strcmp(fileNames[num+1],"empty")!=0)
	      {
		fileCount++;
		fd = open(fileNames[num+1],O_CREAT | O_RDWR);
		close(fd);
	      }
	    else
	      clearArr(fileNames[num+1],128);
	    num++;
	    i++;
	    j=0;
	  }	

}

		
		
/*welcome*/

void welcome()
{ 	
	int i=0;
	for(i=0;i<8;i++)
		printf("\n");

	printf("*****************************************************************************\n");
	printf("****                     welcome to our os                               ****\n");
	printf("****                            made by:       Shen Jinwei   1552756     ****\n");
	printf("****                                           Chen    hui   1552698           ****\n" );
	printf("****                                           Cai  Xuyang   1552734     ****\n");
	printf("****                                           2017  8.15                ****\n");
	printf("*****************************************************************************\n");

	for(i=0;i<8;i++)
		printf("\n");
}
/* colorful */
void colorful()
{
	int j = 0;
	for (j = 0; j < 3200; j++){disp_color_str("S", BLACK);}
	for (j = 0; j < 5; j++)
		disp_color_str("SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS", GREEN);
	

	/*line one*/
	disp_color_str("       **       **",RED);
	disp_color_str("  **********",BLUE);
	disp_color_str("  **",GREEN);
	disp_color_str("          **",RED);
	disp_color_str("         ***********       \n",WHITE);

	/*line two*/
	disp_color_str("  	    **       **",RED);
	disp_color_str("  **********",BLUE);
	disp_color_str("  **",GREEN);
	disp_color_str("          **",RED);
	disp_color_str("         **       **         \n",WHITE);


	/*line three*/
	disp_color_str("  	    **       **",RED);
	disp_color_str("  **        ",BLUE);
	disp_color_str("  **",GREEN);
	disp_color_str("          **",RED);
	disp_color_str("         **       **          \n",WHITE);


	/*line four*/
	disp_color_str("  	    ***********",RED);
	disp_color_str("  **********",BLUE);
	disp_color_str("  **        ",GREEN);	
	disp_color_str("  **",RED);
	disp_color_str("         **       **       \n",WHITE);

	/*line five*/
	disp_color_str("  	    ***********",RED);
	disp_color_str("  **********",BLUE);
	disp_color_str("  **        ",GREEN);
	disp_color_str("  **",RED);
	disp_color_str("         **       **     \n",WHITE);


	/*line six*/
	disp_color_str("  	    **       **",RED);
	disp_color_str("  **       ",BLUE);
	disp_color_str("   **",GREEN);
	disp_color_str("          **",RED);
	disp_color_str("         **       **     \n",WHITE);

	/*line seven*/
	disp_color_str("  	    **       **",RED);
	disp_color_str("  **********",BLUE);
	disp_color_str("  **********",GREEN);
	disp_color_str("  **********",RED);
	disp_color_str(" **       **        \n",WHITE);

	/*line eight*/
	disp_color_str("  	    **       **",RED);
	disp_color_str("  **********",BLUE);
	disp_color_str("  **********",GREEN);
	disp_color_str("  **********",RED);
	disp_color_str(" ***********      \n",WHITE);

		for (j = 0; j < 5; j++)
		disp_color_str("SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS", GREEN);

	milli_delay(8000);


	
}

/* Clear */
PUBLIC void clear()
{
	int i = 0;
	for (i = 0; i < 30; i++)
		printf("\n");
}

int getPos()
{
  int i=0;
  for(;i<20;i++)
    {
      if(strcmp(fileNames[i],curFile)==0)
	return i;
    }
  return 0;
}
/* Show Process */
void showProcess()
{	int i = 0;
	printf("********************************************************************************\n");
	printf("        name        |        priority        |        f_flags(0 is runable)        \n");
	printf("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	for (i = 0; i < NR_TASKS + NR_PROCS; i++)
	{
		printf("        %s                   %d                      %d        %d\n", proc_table[i].name, proc_table[i].priority, proc_table[i].p_flags,i);
	}
	printf("********************************************************************************\n");
}

/* Show Help Message */
void help()
{
	printf("********************************************************************************\n");
	printf("        name               |                      function                      \n");
	printf("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	printf("        welcome            |           Welcome the users\n");
	printf("        clear              |           Clean the screen\n");
	printf("        ls                 |           List all files in current file path\n");
	printf("        help               |           List all commands\n");
	printf("        proc               |           List all process's message\n");
	printf("        caculator          |           Start a simply caculator\n");
	printf("        print  [str]       |           Print a string\n");
	printf("        ct[file]           |           Create a file\n");
	printf("        read   [file]      |           Read a file\n");
	printf("        delete [file]      |           Delete a file\n");
	printf("        edit   [file]      |           Edit file, cover the content\n");
	printf("        newLogin           |           Create a new user\n");
	printf("        login  [user][pass]|           Login \n");
	printf("        push               |           Start push game\n");
	printf("        caculator          |           Open caculator\n");
	printf("        gomoku             |           Start gomoku\n");
	printf("********************************************************************************\n");
	
}

/* Create File */
void createFile(char * fileName)
{
  int fp=-1;
  int curPos=0;                               //current file's position in FAT
  char temp[128];

  clearArr(temp,128);
  if(isExist(fileName)!=-1)
    {
      /* printf("file Exists\n"); */
      return;
    }

  curPos=getPos();

      //printf("curPos:  %d\n",curPos);
      int i=1;
      while(FAT[curPos][i]!=0  && i<fileSize){
  			i++;
      };
      
      if(i==fileSize)
  	     printf("%d file has been full",curPos);
      else
  	{	  
	   fp=open(fileName,O_CREAT | O_RDWR); 
	   assert(fp!=-1); 
	  printf("%s created successfully fp:%d \n",fileName,fp);
	  write(fp,temp,128);
	  close(fp);
	  fileCount++;
	  strcpy(fileNames[fileCount],fileName);       //  add File in Filenames
  	  FAT[curPos][i]=fileCount;
	  FAT[curPos][0]++;
 	 }
}

//if fileName exists in current directory return position
//else return -1
int isExist(char * fileName)
{
  int fp=-1, curPos=0, childFileNums=0, i=0,pos=0;

  curPos=getPos();
  //printf("curPos:%d\n",curPos);
  childFileNums=FAT[curPos][0];
  while(childFileNums>0)
    {   
      pos = FAT[curPos][i+1];
     // printf("curPos:%d\n",curPos);
     // printf("pos:%d\n",pos);
      if(pos!=0)
	{
	  if(strcmp(fileNames[pos],fileName)==0)
	    break;
	 childFileNums--;
	}
       i++;
    }
  
  if(childFileNums>0)
      return pos;
  else{
    return -1;
  }
}

void editFile(char * arg)
{
  int fp=-1,result=-1,r=0;
  char rdbuf[128];

  if(isExist(arg)==-1)
    {
      printf("no such file in current directory,edit fail!\n");
      return;
    }

  printf("please print new file content\n");

  clearArr(rdbuf,128);
  r = read(0, rdbuf, 70);
  rdbuf[r] = 0;
 

  fp = open(arg,O_RDWR);
  result=write(fp,rdbuf,r);
  close(fp);
  
  if(result==-1)
    printf("write error: %s\n",arg);
  else
     printf("write success num: %d \n",result);
}

/* Read File */
void readFile(char * fileName)
{
  int fp=-1;
  char rdbuf[128];
  
 
  clearArr(rdbuf,128);
  
  if(isExist(fileName)==-1)
    {
      printf("read fail no such file!\n");
      return;
    }
  

  fp = open(fileName,O_RDWR);
  assert(fp!=-1);
  int r = read(fp,rdbuf,128);
  assert(r!=-1);
  close(fp);
  rdbuf[r]=0;
  printf("content in %s: %s num: %d\n",fileName,rdbuf,r);
}




/* Delete File */
void deleteFile(char * fileName)
{

int curPos = 0,i=1,j=1,pos=0;

 curPos = getPos();
 pos = isExist(fileName);

 if(pos==-1)
   {
     printf("no %s fileName in current directory delete fail\n ",fileName);
 	return;
   }

 for(i=1;i<fileSize;i++)
   {
     int temp = FAT[curPos][i];
     if(strcmp(fileNames[temp],fileName)==0)
       break;
   }
 FAT[curPos][0]--;
 FAT[curPos][i]=0;
 delete(pos);
 save();
}


void delete(int pos)
{
  unlink(fileNames[pos]);

  printf("unlink succeed\n");
  clearArr(fileNames[pos],128);
  fileCount--;

  if(FAT[pos][0]==0)
    return;
  else
    {
      int num=FAT[pos][0];
      int  i=1;
      while(num>0)
	{
	  if(FAT[pos][i]!=0)
	    {
	      delete(FAT[pos][i]);
	      FAT[pos][i]=0;
	      FAT[pos][0]--;
	      num--;
	    }
	  i++;
	}

    }

}




/* Ls */
void lsFile()
{
	int curPos =0,pos=0,num=0,i=0;
	 
	curPos = getPos();
	num= FAT[curPos][0];
	
	//printf("in lsFile num: %d\n curPos:%d",num,curPos);
	for( i=1;i<fileSize && num>0 ;i++)
	{
	  if(FAT[curPos][i]!=0)
	    {
	      pos = FAT[curPos][i];
	      printf("%s ",fileNames[pos]);
	      num--;
	    }
	}
	 printf("\n");
}


void cdFile(char * fileName)
{
	int pos=-1;
	 	
  	if(strcmp(fileName,"..")==0)
	{
		cdBack();
		return;
	}

	pos = isExist(fileName);
	if(pos==-1)
	   return;


	clearArr(curFile,128);
	strcpy(curFile,fileName);
	strcat(location,"/");
	strcat(location,fileName);	
	
	//printf("curfile:%s\n",curFile);
	
}
void cdBack()
{
	
  int curPos = 0,i=0,j=0,pos=0,len=0;
	
  	curPos= getPos();
	if(curPos==0)
	  return;
	clearArr(curFile,128);
	len = strlen(location);
	i=len-1;
	location[len]='\0';
	while(location[i]!='/')
	  {
	    location[i]='\0';
	    i--;
	  }
	location[i]='\0';
	i--;
	while(i>=0&&location[i]!='/' )
	  {
	    i--;
	  }
	i++;
	while(location[i]!='\0')
	  {
	    curFile[j]=location[i];
	    i++;
	    j++;
	  }
	curFile[j]='\0';
	/* for(i=0;i<=curPos;i++) */
	/* { */
	/*   for(j=1;j<=FAT[i][0];j++) */
	/*      { */
	/* 	pos = FAT[i][j]; */
	/* 	if(strcmp(fileNames[pos],fileNames[curPos])==0) */
	/* 		{	printf("i:%d,j:%d curPos:%d",i,j,curPos); */
	/* 		 	char temp[128]; */
	/* 			clearArr(temp,128); */
	/* 			memcpy(temp,location,strlen(location)-strlen(curFile)-1); */

	/* 			clearArr(location,128); */
	/* 			strcpy(location,temp); */
					
	/* 			clearArr(curFile,128); */
	/* 			strcpy(curFile,fileNames[i]); */
	/* 			printf("curfile:%s\n",curFile); */
	/* 			break; */
	/* 		} */
	/*      } */
	/* }		 */

}






PUBLIC int TESTA(char * topic)
{
	MESSAGE msg;
	reset_msg(&msg);
	msg.type = XIA;
	strcpy(msg.content, topic);
	printf("%s\n", msg.content);
	send_recv(BOTH, TASK_SYS, &msg);
	return msg.RETVAL;
}

	
