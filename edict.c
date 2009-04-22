#include "util.h"
#include "edict.h"


//////////////////////////////////////////////////
// Edict
//////////////////////////////////////////////////

int edict_init(EDICT *edict,LTV *root)
{
    int rval=0;
    LT_init();
    TRY(!edict,-1,done,"edict=0x%x\n",edict);
    TRY(!(CLL_init(&edict->anons)),-1,done,"CLL_init(&edict->anons) failed\n",0);
    TRY(!(CLL_init(&edict->stack)),-1,done,"CLL_init(&edict->stack) failed\n",0);
    TRY(!(CLL_init(&edict->input)),-1,done,"CLL_init(&edict->input) failed\n",0);
    LTV_put(&edict->stack,root,0);
 done:
    return rval;
}

int edict_destroy(EDICT *edict)
{
    int rval;
    TRY(!edict,-1,done,"edict=0x%x\n",edict);
    CLL_release(&edict->anons,LTVR_release);
    CLL_release(&edict->stack,LTVR_release);
    CLL_release(&edict->input,LTVR_release);
 done:
    return rval;
}


#define EDICT_NAMESEP ".,"

int edict_delimit(char *str,int rlen)
{
    int len = (str && *str)?strcspn(str,EDICT_NAMESEP):0;
    return rlen<len?rlen:len;
}

LTV *edict_add(EDICT *edict,LTV *ltv)
{
    return LTV_put(&edict->anons,ltv,0);
}

LTV *edict_rem(EDICT *edict)
{
    return LTV_get(&edict->anons,1,0); // cleanup: LTV_release(LTV *)
}

LTV *edict_name(EDICT *edict,char *name,int len,int end)
{
    LTV *root=LTV_get(&edict->stack,0,0);
    if (len<0) len=strlen(name);
    while(len>0 && root)
    {
        LTI *lti;
        int tlen=edict_delimit(name,len);
        if ((lti=LT_lookup(&root->rbr,name,tlen,1)))
        {
            if (name[tlen]=='.')
                root=LTV_get(&lti->cll,0,0);
            else if (name[tlen]==',')
                root=LTV_get(&lti->cll,0,1);
            else
                return LTV_put(&lti->cll,LTV_get(&edict->anons,1,0),end);
            if (!root)
                root=LTV_put(&lti->cll,LTV_new("",-1,0),0);
            len-=(tlen+1);
            name+=(tlen+1);
        }
    }
    return NULL;
}


LTV *edict_ref(EDICT *edict,char *name,int len,int pop,int end)
{
    LTV *root=LTV_get(&edict->stack,0,0);
    if (len<0) len=strlen(name);
    while(len>0 && root)
    {
        LTI *lti;
        int tlen=edict_delimit(name,len);
        if ((lti=LT_lookup(&root->rbr,name,tlen,0)))
        {
            if (name[tlen]=='.')
                root=LTV_get(&lti->cll,0,0);
            else if (name[tlen]==',')
                root=LTV_get(&lti->cll,0,1);
            else
                return LTV_put(&edict->anons,LTV_get(&lti->cll,pop,end),0);
            len-=(tlen+1);
            name+=(tlen+1);
        }
    }
    return NULL;
}

///////////////////////////////////////////////////////
///////////////////////////////////////////////////////
// PARSER
///////////////////////////////////////////////////////
///////////////////////////////////////////////////////

#define LIT_DELIMIT "'[(){}"

enum { DELIMIT_SIMPLE_LIT_END, DELIMIT_EXP_START, DELIMIT_EXP_END, DELIMIT_COMMENT, DELIMIT_MAX };
char delimiter[DELIMIT_MAX][256];
int edict_bytecode_count;

#define DICT_BYTECODE(dict,opcode,fn)                                                                     \
    {                                                                                                     \
        edict_bytecodes.bytecode[(int) *opcode]=(edict_extension) ((unsigned) *fn);                       \
        if (!strchr(delimiter[DELIMIT_EXP_START],*opcode))                                                \
        {                                                                                                 \
            delimiter[DELIMIT_EXP_START][edict_bytecode_count++]=*opcode;                                 \
            delimiter[DELIMIT_EXP_START][edict_bytecode_count]=(char) NULL;                               \
            stpcpy(stpcpy(delimiter[DELIMIT_SIMPLE_LIT_END],WHITESPACE),delimiter[DELIMIT_EXP_START]);    \
            stpcpy(stpcpy(delimiter[DELIMIT_EXP_END],LIT_DELIMIT),delimiter[DELIMIT_SIMPLE_LIT_END]);     \
        }                                                                                                 \
    }


int edict_balance(char *str,char *start_end)
{
    for(int i=0,nest=0;str[i];i++)
        if (str[i]==start_end[0]) nest++;
        else if (str[i]==start_end[1] && --nest==0)
            return ++i; // balanced
    return 0; // not balanced
}



int edict_thread()
{
    int len=0;
    static char *buf=NULL;
    char *token=NULL;

    while (1)
    {
        if (!buf)
            token=buf=edict_readline();

        while (**token && *(*token+=strspn(*token,WHITESPACE))) // not end of string
        {
            switch (**token)
            {
                case '[': len=edict_balance(*token,"[]"); // lit
                case '\'': return strcspn(*token,delimiter[DELIMIT_SIMPLE_LIT_END]);
                case '(':
                case ')':
                case '{':
                case '}':
                case '<':
                case '>':
                    return 1;
                default:
                    token=strspn(*buf,delimiter[DELIMIT_EXP_START]); // skip over EXP_START
                    return pos+strcspn(*buf+pos,delimiter[DELIMIT_EXP_END]); // skip until EXP_END
            }
        }
    }
    else

    done:
    return 0;
}


int edict_thread(EDICT *edict)
{
    int len=0;
    char *token=NULL;

    while(len=edict_delimit(&token))
        edict_evaluate(edict,token,len);
    
    return 0;
}


/*
void *edict_pop(EDICT *edict,char *name,int len,int end)
{
}

LTV *edict_clone(LTV *ltv,int sibs)
{
}
int edict_copy_item(EDICT *edict,LTV *ltv)
{
}
int edict_copy(EDICT *edict,char *name,int len)
{
}
int edict_raise(EDICT *edict,char *name,int len)
{
}

void *edict_lookup(EDICT *edict,char *name,int len)
{
}

void edict_display_item(LTV *ltv,char *prefix)
{
}
void edict_list(EDICT *edict,char *buf,int len,int count,char *prefix)
{
}

int edict_len(EDICT *edict,char *buf,int len)
{
}

LTV *edict_getitem(EDICT *edict,char *name,int len,int pop)
{
}
LTV *edict_getitems(EDICT *edict,LTV *repos,int display)
{
}

LTV *edict_get_nth_item(EDICT *edict,int n)
{
}
*/

