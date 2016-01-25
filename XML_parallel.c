/************************************************************
Copyright (C).
FileName: XML_parallel.c
Author: Jack
Version : V3.0
Date: 01/18/2015
Description: This program could execute the XPath to get some necessary information from a large XML dataset. 
It could also divide it into several parts and deal with each part in parallel.
History: 
<author> <time> <version> <desc>
1 Jack 01/03/2015 V1.0 build the first version which could implement the main functions of XML parallel processing
2 Jack 01/13/2015 V2.0 optimize the code for split phase to load XML file into memory, not read them directly from original file.
3 Jack 01/18/2015 V3.0 the main function takes number-of-threads as an input parameter without asking the size of a partition, 
make some optimizations on the split phase to get a better performance and merge the sequential version of this algorithm into one program.
4 Jack 01/20/2015 V4.0 remove the active part from the interface and put these into a configuration file
5 Jack 01/24/2015 V5.0 get the start status automatically for each thread
***********************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <malloc.h>
#include <sys/time.h>
#include <sys/file.h>
#include <unistd.h>

/*data structure for each thread*/
#define MAX_THREAD 10
pthread_t thread[MAX_THREAD]; 
int thread_args[MAX_THREAD];
int finish_args[MAX_THREAD];

/*data structure for automata*/
typedef struct{
	int start;
	char * str;
	int end;
	int isoutput; 
}Automata;

#define MAX_SIZE 50
Automata stateMachine[MAX_SIZE];   //save automata for XPath

int stateCount=0; //the number of states for XPath
int machineCount=1; //the number of nodes for automata

/*data structure for the whole status stack*/
#define MAX_OUTPUT 50000000
typedef struct status{
	int stack[MAX_SIZE+1];
	int queue[MAX_SIZE+1];
	int top_stack;
	int bottom_stack;
	int rear_queue;
	int front_queue;
	int hasOutput;
	char** output;
	int topput;
}status;

status state_stack[MAX_THREAD];


/*data structure for files in each thread*/
char * buffFiles[MAX_THREAD]; 

/*data structure for elements in XML file*/
typedef struct
{
    char *p;
    int len;
}
xml_Text;

typedef enum {
    xml_tt_U, /* Unknow */
    xml_tt_H, /* XML Head <?xxx?>*/
    xml_tt_E, /* End Tag </xxx> */
    xml_tt_B, /* Start Tag <xxx> */
    xml_tt_BE, /* Tag <xxx/> */
    xml_tt_T, /* Content for the tag <aaa>xxx</aaa> */
    xml_tt_C, /* Comment <!--xx-->*/
    xml_tt_ATN, /* Attribute Name <xxx id="">*/
    xml_tt_ATV, /* Attribute Value <xxx id="222">*/
    xml_tt_CDATA/* <![CDATA[xxxxx]]>*/
}
xml_TokenType;

typedef struct
{
    xml_Text text;
    xml_TokenType type;
}
xml_Token;

#define MAX_LINE 100
static char multiExpContent[MAX_THREAD][MAX_LINE];  //save for multi-line explanations
static char multiCDATAContent[MAX_THREAD][MAX_LINE]; //save for multi-line CDATA

#define MAX_ATT_NUM 50
char tokenValue[MAX_ATT_NUM][MAX_ATT_NUM]={"UNKNOWN","HEAD","NODE_END","NODE_BEGIN","NODE_BEGIN_END","TEXT","COMMENT","ATTRIBUTE_NAME","ATTRIBUTE_VALUE","CDATA"};
char defaultToken[MAX_ATT_NUM]="WRONG_INFO";

/*data structure for mapping result*/
typedef struct ResultSet
{
	int begin;
	int begin_stack[MAX_SIZE];
	int topbegin;
	int end;
	int end_stack[MAX_SIZE];
	int topend;
}ResultSet;


/*before thread creation*/
int load_file(char* file_name); //load XML into memory(only used for sequential version)
int split_file(char* file_name, int n);  //split XML file into several parts and load them into memory
char* ReadXPath(char* xpath_name);  //load XPath into memory
void createAutoMachine(char* xmlPath);   //create automachine for XPath.txt

/*main functions for each thread*/
void push(int thread_num,int nextState); //push new element into stack
 void pop(int next, int thread_num); //pop element due to end_tag e.g</d>
int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);  //parse and deal with every element in an xmlText, return value:0--success -1--error 1--multiline explantion 2--multiline CDATA

/*functions called by each thread*/
char* substring(char *pText, int begin, int end);
char* convertTokenTypeToStr(xml_TokenType type); //get the type for each element
int xml_initText(xml_Text *pText, char *s);
int xml_initToken(xml_Token *pToken, xml_Text *pText);
char* ltrim(char *s); //reduct blank from left
int left_null_count(char *s);  //calculate the number of blanket for each string

/*get and merge the mappings for the result*/
ResultSet getresult(int n);
void print_result(ResultSet set,int n);


/*************************************************
Function: int split_file(char* file_name,int n);
Description: split a large file into several parts according to the number of threads for this program, while keeping the split XML files into the memory
Called By: int main(void);
Input: file_name--the name for the xml file; n--the number of threads for this program
Return: the number of threads(start with 0); -1--can't open the XML file
*************************************************/
int split_file(char* file_name,int n)
{
	FILE *fp;
    int i,j,k,one_size;
    int size;
    fp = fopen (file_name,"rb");
    if (fp==NULL) { return -1;}
    fseek (fp, 0, SEEK_END);   
    size=ftell (fp);
    rewind(fp);
    one_size=(size/n)+1;
    n=n-1;
    char ch=-1;
    int temp_one_size=0;
    int s;
    for (i=0;i<n;i++)
    {
    	s=0;
        buffFiles[i]=(char*)malloc((one_size+MAX_LINE)*sizeof(char));
        if(ch!=-1)
        {
    	    buffFiles[i][0]=ch;
    	    buffFiles[i][1]='\0';
    	    char * buff=(char*)malloc((one_size)*sizeof(char));
    	    k = fread (buff,1,one_size-1,fp);
    	    buff[one_size-1]='\0'; 
    	    buffFiles[i]=strcat(buffFiles[i],buff);
    	    free(buff);
	    }
	    else
	    {
	    	k = fread (buffFiles[i],1,one_size,fp);
		}
        /*skip the default size to look for the next open angle bracket*/
        ch=fgetc(fp);
        while(ch!='<')
        {
        	buffFiles[i][one_size+(s++)]=ch;
        	ch=fgetc(fp);
        	temp_one_size++;
		}
		buffFiles[i][one_size+s]='\0';
    }
    j = size % one_size-temp_one_size;
    if (j!=0) {
    buffFiles[n]=(char*)malloc((j+1)*sizeof(char));
    if(ch!=-1)
    {
    	buffFiles[n][0]=ch;
    	buffFiles[n][1]='\0';
    	char * buff=(char*)malloc((j+1)*sizeof(char));
    	k = fread (buff,1,j,fp);
    	buff[j+1]='\0'; 
    	buffFiles[n]=strcat(buffFiles[n],buff);
    	buffFiles[n][j]='\0'; 
    	free(buff);
	}
    else
    {
    	k = fread (buffFiles[n],1,j,fp);
        buffFiles[n][j]='\0';  
	}
	
    }
    fclose(fp);
    return n;
}

/*************************************************
Function: int load_file(char* file_name);
Description: load the XML file into memory(only used for sequential version)
Called By: int main(void);
Input: file_name--the name for the xml file
Return: 0--load successful; -1--can't open the XML file
*************************************************/
int load_file(char* file_name)
{
	FILE *fp;
    int i,j,k,n;
    int size;
    fp = fopen (file_name,"rb");
    if (fp==NULL) { return -1;}
    fseek (fp, 0, SEEK_END);   
    size=ftell (fp);
    rewind(fp);
    buffFiles[0]=(char*)malloc((size+1)*sizeof(char));
    k = fread (buffFiles[0],1,size,fp);
    buffFiles[0][size]='\0'; 
    fclose(fp);
    return 0;
}

/*************************************************
Function: char* ReadXPath(char* xpath_name);
Description: load XPath from related file
Called By: int main(void);
Input: xpath_name--the name for the XPath file
Return: the contents in the Xpath file; error--can't open the XPath file
*************************************************/
char* ReadXPath(char* xpath_name)
{
	FILE *fp;
	char* buf=(char*)malloc(MAX_LINE*sizeof(char));
	char* xpath=(char*)malloc(MAX_LINE*sizeof(char));
	xpath=strcpy(xpath,"");
	if((fp = fopen(xpath_name,"r")) == NULL)
    {
        xpath=strcpy(xpath,"error");
    }
    else{
    	while(fgets(buf,MAX_LINE,fp) != NULL)
    	{
    		xpath=strcat(xpath,buf);
		}
	}
	free(buf);
    return xpath;
}

/*************************************************
Function: void createAutoMachine(char* xmlPath);
Description: create an automata by the XPath Query command
Called By: int main(void);
Input: xmlPath--XPath Query command
*************************************************/
void createAutoMachine(char* xmlPath)
{
	char seps[] = "/"; 
	char *token = strtok(xmlPath, seps); 
	while(token!= NULL) 
	{
		stateCount++;
		stateMachine[machineCount].start=stateCount;
		stateMachine[machineCount].str=(char*)malloc((strlen(token)+1)*sizeof(char));
		stateMachine[machineCount].str=strcpy(stateMachine[machineCount].str,token);
		stateMachine[machineCount].end=stateCount+1;
		stateMachine[machineCount].isoutput=0;
		machineCount++;
		if(stateCount>=1)
		{
			stateMachine[machineCount].start=stateCount+1;
			stateMachine[machineCount].str=(char*)malloc((strlen(stateMachine[machineCount-1].str)+2)*sizeof(char));
			stateMachine[machineCount].str=strcpy(stateMachine[machineCount].str,"/");
			stateMachine[machineCount].str=strcat(stateMachine[machineCount].str,stateMachine[machineCount-1].str);
			stateMachine[machineCount].end=stateCount;
			stateMachine[machineCount].isoutput=0;
		}
		token=strtok(NULL,seps);  
		if(token==NULL)
		{
			stateMachine[machineCount-1].isoutput=1;
			stateMachine[machineCount].isoutput=1;
		}
		else machineCount++;
	}
    stateCount++;
}

/*************************************************
Function: void push(int thread_num,int nextState);
Description: push the next state into stack
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: thread_num--the number of thread;nextState--the next state;
*************************************************/
void push(int thread_num,int nextState) 
{
	state_stack[thread_num].stack[state_stack[thread_num].top_stack++]=nextState;
}


/*************************************************
Function: void pop(int next, int thread_num);
Description: if type of the xml element is End Tag(e.g </xxx>) and the content of the tag could be found in the automata, 
then this function would delete the related state from the stack of state_stack. If no such node exists, the next state will
be pushed into the queue of state_stack.
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: next--the next state;thread_num--the number of thread;
*************************************************/
void pop(int next, int thread_num) //pop element due to end_tag e.g</d>
{
	int stack_top=-1;
	int top=state_stack[thread_num].top_stack;
	int rear;
	if(state_stack[thread_num].top_stack>1)
	    stack_top=state_stack[thread_num].stack[top-2];  
	if(stack_top!=-1&&stack_top==next) //for state next
	{
		state_stack[thread_num].top_stack--;
	}
	else //not in final stack, add it into the start queue
	{
		state_stack[thread_num].stack[top-1]=next;
		rear=state_stack[thread_num].rear_queue++;
		state_stack[thread_num].queue[rear]=next;
	}
}

/*************************************************
Function: char * convertTokenTypeToStr(xml_TokenType type);
Description: convert the XML token type from digit to the real string for output
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: type--the enumeration for the type of XML
Return: the output string for this type
*************************************************/
char * convertTokenTypeToStr(xml_TokenType type)
{

	return tokenValue[type];
	//else return defaultToken;
}

/*************************************************
Function: int xml_initText(xml_Text *pText, char *s);
Description: initiate a xml_Text for a string loading from original XML file
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: pText--the xml_Text element waiting to be initialized; s--the XML string;
Output: pText--the initialized xml_Text
Return: 0--success
*************************************************/
int xml_initText(xml_Text *pText, char *s)
{
    pText->p = s;
    pText->len = strlen(s);
    return 0;
}

/*************************************************
Function: xml_initToken(xml_Token *pToken, xml_Text *pText);
Description: initiate a xml_Token for a initialized xml_Text
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: pToken--the xml_Token element waiting to be initialized; pText--input xml_Text;
Output: pToken--the initialized xml_Token
Return: 0--success
*************************************************/
int xml_initToken(xml_Token *pToken, xml_Text *pText)
{
    pToken->text.p = pText->p;
    pToken->text.len = 0;
    pToken->type = xml_tt_U;
    return 0;
}

/*************************************************
Function: int xml_print(xml_Text *pText, int begin, int end);
Description: print the substring of a xml_Text
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: pText--input xml_Text; begin--start position; end--end position;
Output: the substring of a xml_Text
Return: 0--success
*************************************************/
int xml_print(xml_Text *pText, int begin, int end)
{
    int i;
    char * temp=pText->p;
    temp = ltrim(pText->p);
    int j=0;
    for (i = begin; i < end; i++)
    {
        putchar(temp[i]);
    }
    return 0;
}

/*************************************************
Function: char* substring(char *pText, int begin, int end);
Description: print the substring of the original string
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: pText--the original string; begin--start position; end--end position;
Return: the final string
*************************************************/
char* substring(char *pText, int begin, int end)
{
    int i,j;
    char * temp=pText;
    temp = ltrim(pText);
    char* temp1=(char*)malloc((end-begin+1)*sizeof(char));
    for (j = 0,i = begin; i < end; i++,j++)
    {
        temp1[j]=temp[i];
    }
    temp1[j]='\0';
    return temp1;
}

/*************************************************
Function: char * ltrim(char *s);
Description: remove the left blankets of a string
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: s--the original string; 
Return: the final substring
*************************************************/
char * ltrim(char *s)
{
     char *temp;
     temp = s;
     while((*temp == ' ')&&temp){*temp++;}
     return temp;
}

/*************************************************
Function: int left_null_count(char *s);
Description: calculate the number of blanket for each string
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: s--the original string; 
Return: the number of blanket for each string
*************************************************/
int left_null_count(char *s)  
{
	 int count=0;
     char *temp;
     temp = s;
     while((*temp == ' ')&&temp){*temp++; count++;}
     return count;
}

/*************************************************
Function: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Description: the function could be called by each thread, dealing with each line of the file. Besides, this function could identify the following elements, 
which include XML head, Start Tag(e.g <xxx>), End Tag(e.g </xxx>), Tag(e.g <xxx/>), Content for the Tag, XML Explanation, Attribute Name for Tag, 
Attribute Value for Tag, Content for CDATA element. Each element would be processed according to its type. 
Called By: int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num);
Input: pText-the content of the xml file; pToken-the type of the current xml element; multilineExp-whether the current line of the xml file is the multiline explanation; 
multilineCDATA-- whether the current line of the xml file is the multiline CDATA; thread_num-the number of the thread; 
Return: 0--success -1--error 1--multiline explantion 2--multiline CDATA
*************************************************/
int xml_process(xml_Text *pText, xml_Token *pToken, int multilineExp, int multilineCDATA, int thread_num)  
{
    char *start = pToken->text.p + pToken->text.len;
    char *p = start;
    char *end = pText->p + pText->len;
    int state = 0;
    int templen = 0;
    if(multilineExp == 1) state = 10;   //1--multiline explantion  0--single line explantion
    if(multilineCDATA == 1) state = 17; //1--multiline CDATA 0--single CDATA
    int j,a;
    int flag=0; //whether the correct start state has been found 0--not found 1--found

    pToken->text.p = p;
    pToken->type = xml_tt_U;
    
    for (; p < end; p++)
    {
    	//printf("p %s\n",p);
        switch(state)
        {
            case 0:
               switch(*p)
               {
                   case '<':
                   	   
                       state = 1;
                       break;
                   case ' ':
                   	   state = 0;
                   	   break;
                   default:
                       state = 7;
                       break; 
               }
            break;
            case 1:
               switch(*p)
               {
                   case '?':
                       state = 2;
                       break;
                   case '/':
                       state = 4;
                       break;
                   case '!':
                   	   state = 8;
                   	   break;
                   case ' ':
                   	   state = -1;
                   	   break;
                   default:
                       state = 5;
                       break;
               }
            break;
            case 2:
               switch(*p)
               {
                   case '?':
                       state = 3;
                       break;
                   default:
                       state = 2;
                       break;
               }
            break;
            case 3:
               switch(*p)
               {
               	   putchar(*p);
                   case '>':                        /* Head <?xxx?>*/
                       pToken->text.len = p - start + 1;
                       //pToken->type = xml_tt_H;
                       //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                       //printf("%s","content=");
                       templen = pToken->text.len;
                       //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                       //xml_print(&pToken->text, 0 ,pToken->text.len);
                       //printf(";\n\n");
                       pToken->text.p = start + templen;
                       start = pToken->text.p;
                       state = 0;
                       break;
                   default:
                       state = -1;
                       break;
               }
               break;
            case 4:
                switch(*p)
                {
                   case '>':              /* End </xxx> */
                       pToken->text.len = p - start + 1;
                       //pToken->type = xml_tt_E;
                       //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                       //printf("%s","content=");
                       //xml_print(&pToken->text, 2 , pToken->text.len-1);
                       //printf(";\n\n");
                       
                       char* subs=substring(pToken->text.p , 1 , pToken->text.len-1-left_null_count(pToken->text.p));
					   if(subs!=NULL){
					       for(j=machineCount;j>=1;j=j-2)
                           {
                               if(strcmp(subs,stateMachine[j].str)==0)
                                  break;
	                       }
	                       if(j>=1){
	                       	   int begin=stateMachine[j].start;
	                       	   int end=stateMachine[j].end;
	                       	   if(flag==0)
	                       	   {
                                   state_stack[thread_num].queue[state_stack[thread_num].rear_queue++]=begin;
                                   state_stack[thread_num].stack[state_stack[thread_num].top_stack++]=begin;
	                       	       flag=1;
							   }
							   pop(end,thread_num);
                           }
                           free(subs);
                       }
                       pToken->text.p = start + pToken->text.len;
                       start = pToken->text.p;
                       state = 0;
                       break;
                   case ' ':
                   	   state = -1;
                   	   break;
                   default:
                       state = 4;
                       break;
                }
                break;
            case 5:
                switch(*p)
                {
                   case '>':               /* Begin <xxx> */
                       pToken->text.len = p - start + 1;
                       //pToken->type = xml_tt_B;
                       if(pToken->text.len-1 >= 1){
                       	   //layer++;
                       	   //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                           //printf("%s","content=");
                           templen = pToken->text.len;
                           //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                           
                       	   //xml_print(&pToken->text , 1 , pToken->text.len-1);
                       	   
                           //printf(";\n\n");
                           char* sub=substring(pToken->text.p , 1 , pToken->text.len-1-left_null_count(pToken->text.p));
                           
                           for(j=machineCount-1;j>=1;j=j-2)
                           {
                           	   if(strcmp(sub,stateMachine[j].str)==0)
                           	   {
                           	      break;
							   }
						   }
                            if(sub!=NULL)  free(sub);
						   if(j>=1)  
						   {
						   	    int begin=stateMachine[j].start;
								int end=stateMachine[j].end;
								
						   	    if(flag==0)
	                       	    {
                                    state_stack[thread_num].queue[state_stack[thread_num].rear_queue++]=begin;
                                    state_stack[thread_num].stack[state_stack[thread_num].top_stack++]=begin;
	                       	        flag=1;
							    }
							    push(thread_num,end);								
						   }
					   }
					   else templen = 1;
                       pToken->text.p = start + templen;
                       start = pToken->text.p;
                       
                       state = 0;
                       break;
                   case '/':
                       state = 6;
                       break;
                   case ' ':                 /* Begin <xxx> */
                   	   pToken->text.len = p - start + 1;
                   	   templen = 0;
                      // pToken->type = xml_tt_B;
                       if(pToken->text.len-1 >= 1)
                       {
                       	   //layer++;
                       	   //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                       	   //printf("%s","content=");
                       	   templen = pToken->text.len;
                       	   //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                       	   //xml_print(&pToken->text , 1 , pToken->text.len-1);
                       	   //printf(";\n\n");
                       	   char* sub=substring(pToken->text.p , 1 , pToken->text.len-1-left_null_count(pToken->text.p));  
                       	   
                           for(j=machineCount-1;j>=1;j=j-2)
                           {
                           	   if(strcmp(sub,stateMachine[j].str)==0)
                           	   {
                           	      break;
							   }
						   }
						   if(sub) free(sub);
						   if(j>=1)   
						   {
						   	    int begin=stateMachine[j].start;
								int end=stateMachine[j].end;
						   	    if(flag==0)
	                       	    {
                                    state_stack[thread_num].queue[state_stack[thread_num].rear_queue++]=begin;
                                    state_stack[thread_num].stack[state_stack[thread_num].top_stack++]=begin;
	                       	        flag=1;
							    }
							    push(thread_num,end);
						   }
					   }
					    
                       pToken->text.p = start + templen;
                       start = pToken->text.p;
                   	   state = 13;
                   	   break;
                   default:
                       state = 5;
                   break;
                }
                break;
            case 6:
                switch(*p)
                {
                   case '>':   /* Begin End <xxx/> */
                       pToken->text.len = p - start + 1;
                       //pToken->type = xml_tt_BE;
                       //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer+1);
                       //printf("%s","content=");
                       templen = pToken->text.len;
                       //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                       //xml_print(&pToken->text , 1 , pToken->text.len-2);
                       //printf(";\n\n");
                       pToken->text.p = start + templen;
                       start = pToken->text.p;
                       state = 0;
                       break;
                   default:
                       state = -1;
                   break;
                } 
                break;
            case 7:
                switch(*p)
                {
                   case '<':     /* Text xxx */
                       p--;
                       pToken->text.len = p - start + 1;
                       //pToken->type = xml_tt_T;
                       
                       //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                       //printf("%s","content=");
                       
                       templen = pToken->text.len;
                       if(j>=1&&j<machineCount&&stateMachine[j].isoutput==1)
					   {
					        char* sub=substring(pToken->text.p , 0 , pToken->text.len-left_null_count(pToken->text.p));
					        state_stack[thread_num].output[state_stack[thread_num].topput]=(char*)malloc((strlen(sub)+1)*sizeof(char));
					        state_stack[thread_num].output[state_stack[thread_num].topput]=strcpy(state_stack[thread_num].output[state_stack[thread_num].topput],sub);
					        state_stack[thread_num].topput++;
					        if(sub!=NULL) free(sub);
					        j=-1;
					   }
				       pToken->text.p = start + templen;
                       start = pToken->text.p;
                       state = 0;
				       
                       break;
                   
                   default:
                       state = 7;
                       break;
                }
                break;
            case 8:
            	switch(*p)
            	{
            		case '-':
            			state = 9;
            			break;
            		case '[':
            			if(*(p+1)=='C'&&*(p+2)=='D'&&*(p+3)=='A'&&*(p+4)=='T'&&*(p+5)=='A')
            			{
            				state = 16;
            				p += 5;
            				break;
						}
						else
						{
							state = -1;
							break;
						}
            		default:
            			state = -1;
            			break;
				}
			    break;
			case 9:
				switch(*p)
				{
					case '-':
						state = 10;
						break;
					default:
						state = -1;
						break;
				}
			    break;
			case 10:
				switch(*p)
				{
					case '-':
						state = 11;
						break;
					default:
						state = 10;
						break;
				}
			    break;
			case 11:
				switch(*p)
				{
					case '-':
						state = 12;
						break;
					default:
						state = -1;
						break;
				}
			    break;
			case 12:
				switch(*p)
				{
					case '>':                            /* Comment <!--xx-->*/
					    pToken->text.len = p - start + 1;
                        //pToken->type = xml_tt_C;
                        //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                        //printf("%s","content=");
                        templen = pToken->text.len;
                        //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                        if(multilineExp == 1)
                        {
                        	strcat(multiExpContent[thread_num],pToken->text.p);
                            //printf("%s",ltrim(multiExpContent[thread_num]));
                            //printf("\n");
                            memset(multiExpContent[thread_num], 0 , sizeof(multiExpContent[thread_num]));
						}
						/*else{
							xml_print(&pToken->text , 0 , pToken->text.len);
                            printf(";\n\n");
						}*/
						
                        pToken->text.p = start + templen;
                        start = pToken->text.p;
                        state = 0;
						break;
					default:
						state = -1;
						break;
				}
			    break;
			case 13:
				switch(*p)
				{
					case '>':
						state = -1;
						break;
					case '=':                       /*attribute name*/
					    pToken->text.len = p - start + 1;
                        //pToken->type = xml_tt_ATN;
                        //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                        //printf("%s","content=");
                        templen = pToken->text.len;
                        //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                        //xml_print(&pToken->text, 0 , pToken->text.len-1);
                        //printf(";\n\n");
                        pToken->text.p = start + templen;
                        start = pToken->text.p;
						state = 14;
						break;
					default:
						state = 13;
						break;
				}
			    break;
			case 14:
				switch(*p)
				{
					case '"':                                       
                   	    state = 15;
						break;
					case ' ':
						state = 14;
						break;
					default:
						state = -1;
						break;
				}
			    break;	
			case 15:
				switch(*p)
				{
					case '"':                        /*attribute value*/
						pToken->text.len = p - start + 1;
                        //pToken->type = xml_tt_ATV;
                        //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                        //printf("%s","content=");
                        templen = pToken->text.len;
                        //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                        //xml_print(&pToken->text, 1 , pToken->text.len-1);
                        //printf(";\n\n");
                        pToken->text.p = start + templen;
                        start = pToken->text.p;
                        state = 5;
						break;
					default:
						state = 15;
						break;
				}
			    break;
			case 16:
				switch(*p)
				{
					case '[':                                       
                   	    state = 17;
						break;
					default:
						state = -1;
						break;
				}
			    break;	
			case 17:
				switch(*p)
				{
					case ']':                                       
                   	    state = 18;
						break;
					default:
						state = 17;
						break;
				}
			    break;	
			case 18:
				switch(*p)
				{
					case ']':                                       
                   	    state = 19;
						break;
					default:
						state = -1;
						break;
				}
			    break;	
			case 19:
				switch(*p)
				{
					case '>':                                       
                   	    pToken->text.len = p - start + 1;
                        //pToken->type = xml_tt_CDATA;
                        //printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
                        //printf("%s","content=");
                        templen = pToken->text.len;
                        //pToken->text.len -= strlen(pToken->text.p)-strlen(ltrim(pToken->text.p));
                        if(multilineCDATA == 1)
                        {
                        	strcat(multiCDATAContent[thread_num],pToken->text.p);
                        	/*for( j = 10;j < strlen(multiCDATAContent[thread_num]) - 3;j++)
                        	{
                        		if(multiCDATAContent[thread_num][j] == ']' && multiCDATAContent[thread_num][j+1] == ']') break;
                        		putchar(multiCDATAContent[thread_num][j]);
							}
                            printf("\n");*/
                            memset(multiCDATAContent[thread_num], 0 , sizeof(multiCDATAContent[thread_num]));
						}
						/*else{
							xml_print(&pToken->text , 9 , pToken->text.len-3);
                            printf(";\n\n");
						}*/
						
                        pToken->text.p = start + templen;
                        start = pToken->text.p;
                        state = 0;
						break;
					default:
						state = -1;
						break;
				}
			    break;	
				
            default:  
                state = -1;
                break;
        }
    }
    if(state==-1) {return -1;}
    /*else if(state == 10)
	{
		strcat(multiExpContent[thread_num],ltrim(pToken->text.p));
		return 1;
	} 
	else if(state == 17)
	{
		strcat(multiCDATAContent[thread_num],ltrim(pToken->text.p));
		return 2;
	}*/
	else if(state == 7)
	{
		p--;
        pToken->text.len = p - start + 1;
        if(pToken->text.len>1)
        {
        	//printf("type=%s;  depth=%d;  ", convertTokenTypeToStr(pToken->type) , layer);
            //printf("%s","content=");
            //xml_print(&pToken->text, 0 , pToken->text.len);
            //printf(";\n\n");
            if(j>=1&&j<machineCount&&stateMachine[j].isoutput==1)
			{
				char* sub=substring(pToken->text.p , 0 , pToken->text.len-left_null_count(pToken->text.p));
				state_stack[thread_num].output[state_stack[thread_num].topput]=(char*)malloc((strlen(sub)+1)*sizeof(char));
			    state_stack[thread_num].output[state_stack[thread_num].topput]=strcpy(state_stack[thread_num].output[state_stack[thread_num].topput],sub);
				state_stack[thread_num].topput++;
				if(sub!=NULL) free(sub);
			}
        }
		return 0;
	}
    else return 0;
}

/*************************************************
Function: ResultSet getresult(int n) ;
Description: get all the mappings for the state_stack of the related thread, then merged them into one final mapping. 
Called By: int main(void);
Input: n-total number for all the threads; 
Return: the final mapping set
*************************************************/
ResultSet getresult(int n) 
{
	ResultSet final_set,set;
	int i,j,k;
	final_set.topbegin=0;
	final_set.topend=0;
    final_set.begin=0;final_set.end=0;
    int start=1;
    set.begin=start;
    for(i=0;i<=n;i++)
    {
    	set.begin=start;set.end=0;
    	set.topbegin=0;set.topend=0;
		j=0;
		//deal with the start queue
		if(state_stack[i].top_stack==0)
		{
			final_set.begin=-1;
		    break;
	    }
	    for(j=1;j<state_stack[i].rear_queue;j++)
	    {
	    	set.begin_stack[set.topbegin++]=state_stack[i].queue[j];
		}
		//deal with the final stack
		if(state_stack[i].top_stack==1)
		    set.end_stack[set.topend++]=state_stack[i].stack[0];
		for(j=1;j<state_stack[i].top_stack;j++)
		{
			set.end_stack[set.topend++]=state_stack[i].stack[j-1];
		}
		set.end=set.end_stack[set.topend-1];
		set.topend--;
		//merge finalset&set
	    if(i>0&&final_set.end!=set.begin)
	    {
		    final_set.begin=-1;
		    break;
	    }
	    else{
	        final_set.end=set.end;
	        if(i==0)
	        {
		        final_set.begin=set.begin;
		        for(k=0;k<set.topbegin;k++)
		        {
			        final_set.begin_stack[final_set.topbegin++]=set.begin_stack[k];
		        }
		        for(k=0;k<set.topend;k++)
		        {
			        final_set.end_stack[final_set.topend++]=set.end_stack[k];
		        }
            }
            else{
	            int equal_flag=0;
	            if(final_set.topend==set.topbegin)
	            {
		            for(k=0;k<set.topbegin;k++)
		            {
			            if(final_set.end_stack[set.topbegin-k-1]!=set.begin_stack[k])
			            {
				            equal_flag=1;
				            break;
			            }
		            }
	            }
                if(equal_flag==0)
                {
                	final_set.topend=set.topend;         //end_stack is equal to the current set
	            	for(k=0;k<set.topend;k++)
    	            {
    		            final_set.end_stack[k]=set.end_stack[k];  
		            }
	            }
	            else
	            {
	            	for(k=0;k<set.topend;k++)
    	            {
    		            final_set.end_stack[final_set.topend++]=set.end_stack[k];  //merge
		            }
				}
            }

            start=final_set.end;
    	}
	}
	return final_set;
}
/*************************************************
Function: void print_result(ResultSet set, int n);
Description: print the result mapping set. 
Called By: int main(void);
Input: set-result mapping set;n--the number of threads 
*************************************************/
void print_result(ResultSet set,int n)
{
	if(set.begin==-1)
	{
		printf("The mapping for this part is null, please check the XPath command.\n");
		return;
	}
	int i;
	printf("The mapping for this part is: %d,  ",set.begin);
	for(i=0;i<set.topbegin;i++)  
	{
		printf("%d:",set.begin_stack[i]);
	}
    printf(",  ");
	printf("%d,  ",set.end);
	for(i=set.topend-1;i>=0;i--)
	{
		printf("%d:",set.end_stack[i]);
	}
	printf(",  ");
	int j;
	for(i=0;i<=n;i++)
	{
		for(j=0;j<state_stack[i].topput;j++)
		{
			printf("%s ",state_stack[i].output[j]);
		}
	}
	printf("\n");
}

/*************************************************
Function: void *main_thread(void *arg);
Description: main function for each thread. 
Called By: int main(void);
Input: arg--the number of this thread; 
*************************************************/
void *main_thread(void *arg)
{
	int i=(int)(*((int*)arg));
	printf("start to deal with thread %d.\n",i);
	int ret = 0;
    xml_Text xml;
    xml_Token token;               
    int multiExp = 0; //0--single line explanation 1-- multiline explanation
    int multiCDATA = 0; //0--single line CDATA 1-- multiline CDATA
    
    int j;
    state_stack[i].hasOutput=0;
    state_stack[i].top_stack=0;
    state_stack[i].rear_queue=0;
    state_stack[i].topput=0;
    state_stack[i].output=(char**)malloc(MAX_OUTPUT*sizeof(char*));
	printf("State stack has been initialized for thread %d.\n",i);
    xml_initText(&xml,buffFiles[i]);
    xml_initToken(&token, &xml);
    ret = xml_process(&xml, &token, multiExp, multiCDATA, i);
    free(buffFiles[i]);
    if(ret==-1)
    {
    	printf("There is something wrong with your XML format, please check it!\n");
    	printf("finish dealing with thread %d.\n",i);
    	return NULL;
	}
    finish_args[i]=1;
    printf("finish dealing with thread %d.\n",i);
	return NULL;
}

/*************************************************
Function: void thread_wait(int n);
Description: waiting for all the threads finish their tasks. 
Called By: int main(void);
Input: n--the number of all threads; 
*************************************************/
void thread_wait(int n)
{
	int t;
	while(1){
		for( t = 0; t <= n; t++)
	    {
	    	if(finish_args[t]==0)
	    	{
	    		break;
			}
	    }
	    if(t>n) break;
	   usleep(10000);
	}   
}

/*************************************************
Function: void main_function();
Description: main function for sequential version. 
Called By: int main(void);
*************************************************/
void main_function()
{
	printf("begin dealing with the state tree.\n");
	int ret = 0;
    xml_Text xml;
    xml_Token token;               
    int multiExp = 0; //0--single line explanation 1-- multiline explanation
    int multiCDATA = 0; //0--single line CDATA 1-- multiline CDATA
    int i=0;
    int j;
    state_stack[i].hasOutput=0;
    state_stack[i].top_stack=0;
    state_stack[i].rear_queue=0;
    state_stack[i].topput=0;
    state_stack[i].output=(char**)malloc(MAX_OUTPUT*sizeof(char*));
	printf("State stack has been initialized.\n");
    xml_initText(&xml,buffFiles[i]);
    xml_initToken(&token, &xml);
    ret = xml_process(&xml, &token, multiExp, multiCDATA, i);
    free(buffFiles[i]);
    if(ret==-1)
    {
    	printf("There is something wrong with your XML format, please check it!\n");
    	printf("finish dealing with the state tree.\n");
    	return;
	}
    finish_args[i]=1;
    printf("finish dealing with the state tree.\n");
}

/*********************************************************************************************/
int main(void)
{
	struct timeval begin,end;
	double duration;
    int ret = 0;
   
    char * xpath_name=malloc(MAX_SIZE*sizeof(char));
    xpath_name=strcpy(xpath_name,"config");
    int choose=-1;
    int n=-1;
    char* file_name=NULL;
    char* xmlPath=NULL;
    //read some parameters from config
	FILE *fp;
	char* buf=(char*)malloc(MAX_LINE*sizeof(char));
	char seps[] = "="; 
	char *token_line=NULL; 
	int line=0;
	if((fp = fopen(xpath_name,"rb")) == NULL)
    {
        printf("There is something wrong with the config file, we can not load it. Please check whether it is placed in the right place.\n");
    	exit(1);
    }
    else{
    	while(fgets(buf,MAX_LINE,fp) != NULL)
    	{
    		token_line = strtok(buf, seps); 
    		if(strcmp(token_line,"File_Name")==0)
    		{
    			token_line=strtok(NULL,seps);
    			if(token_line!=NULL)
    			{
    				file_name=malloc(MAX_SIZE*sizeof(char));
    				file_name=strcpy(file_name,token_line);
    				file_name[strlen(file_name)-2]='\0';
				}
			}
    		else if(strcmp(token_line,"XPath")==0)
    		{
    			token_line=strtok(NULL,seps);
    			if(token_line!=NULL)
    			{
    				xmlPath=malloc(MAX_SIZE*sizeof(char));
    				xmlPath=strcpy(xmlPath,token_line);
    				xmlPath[strlen(xmlPath)-2]='\0';
				}
			}
			else if(strcmp(token_line,"version(0--sequential, 1--parallel)")==0)
			{
				token_line=strtok(NULL,seps);
				if(token_line!=NULL)
				{
					sscanf(token_line,"%d",&choose);
				}
			}
			else if(strcmp(token_line,"number-of-threads(no less than 1 and no more than 10)")==0)
			{
				token_line=strtok(NULL,seps);
				if(token_line!=NULL)
				{
					sscanf(token_line,"%d",&n);
				}
			}
		}
	}
	free(buf);
	fclose(fp);

    //judge the version of program
    printf("Welcome to the XML lexer program! Your file name is %s\n\n",file_name);
    if(file_name==NULL)
    {
    	printf("The File_Name in config can not be empty, please open the file and check it again!\n");
    	exit(1);
	}
	if(xmlPath==NULL)
	{
		printf("The XPath in config can not be empty, please open the file and check it again!\n");
    	exit(1);
	}
    if(choose!=0&&choose!=1)
    {
    	printf("The number of version(0--sequential, 1--parallel) in config is not correct, please open the file and check it again!\n");
    	exit(1);
	}

    if(choose==1)
	{
        if((n<1)||(n>10))
        {
    	    printf("The number-of-threads(no less than 1 and no more than 10) in config is not correct, please open the file and check it again!\n");
    	    exit(1);
	    }
	}
	//deal with the file
    printf("begin to split the file\n");
    gettimeofday(&begin,NULL);
    if(choose==0){
    	n=load_file(file_name);    //load file into memory
	}
    else n=split_file(file_name,n);    //split file into several parts
    printf("finish cutting the file!\n");
    gettimeofday(&end,NULL);   
    duration=1000000*(end.tv_sec-begin.tv_sec)+end.tv_usec-begin.tv_usec; 
    printf("The duration for spliting the file is %lf\n",duration/1000000);
    sleep(1);
        
    if(n==-1)
    {
    	printf("There are something wrong with the xml file, we can not load it. Please check whether it is placed in the right place.\n");
    	exit(1);
	}

	printf("\nbegin to deal with XML file\n");
	gettimeofday(&begin,NULL);

    createAutoMachine(xmlPath);     //create automata by xmlpath
    printf("The basic structure of the automata is (from to end):\n");
    int i,rc;
    char *out=" is an output";
    for(i=1;i<=machineCount;i=i+2)
    {
    	if(i==1){
    		printf("%d",stateMachine[i].start);
		}
		printf(" (str:%s",stateMachine[i].str);
		if(stateMachine[i].isoutput==1)
		{
			printf("%s",out);
		}
		printf(") %d",stateMachine[i].end);
	}
	printf("\n");
	for(i=machineCount;i>0;i=i-2)
    {
    	if(i==machineCount){
    		printf("%d (str:%s) %d",stateMachine[i].start,stateMachine[i].str,stateMachine[i].end);
		}
		else
		{
			printf(" (str:%s) %d",stateMachine[i].str,stateMachine[i].end);
		}	
	}
	printf("\n\n");
	if(choose==0)
	{
		main_function();
	}
	else
	{
		for(i=0;i<=n;i++)
        {
    	    thread_args[i]=i;
    	    finish_args[i]=0;
    	    rc=pthread_create(&thread[i], NULL, main_thread, &thread_args[i]);  //parallel xml processing
    	    if (rc)
            {
                printf("ERROR; return code is %d\n", rc);
                return EXIT_FAILURE;
            }
	    }
	    thread_wait(n);
	}
	printf("\nfinish dealing with the file\n");
	gettimeofday(&end,NULL);
    duration=1000000*(end.tv_sec-begin.tv_sec)+end.tv_usec-begin.tv_usec; 
    printf("The duration for dealing with the file is %lf\n",duration/1000000);
    printf("\n");
	printf("All the subthread ended, now the program is merging its results.\n");
	printf("begin to merge results\n");
	gettimeofday(&begin,NULL);
	ResultSet set=getresult(n);
	printf("The mappings for text.xml is:\n");
	print_result(set,n);
	printf("finish merging these results.\n");
    gettimeofday(&end,NULL);
    duration=1000000*(end.tv_sec-begin.tv_sec)+end.tv_usec-begin.tv_usec; 
    printf("The duration for merging these results is %lf\n",duration/1000000);
    
    //system("pause");
    return 0;
}
