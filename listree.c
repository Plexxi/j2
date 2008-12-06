#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include "util.h"
#include "listree.h"


CLL ltv_repo,ltvr_repo,lti_repo;

//////////////////////////////////////////////////
// Circular Linked List
//////////////////////////////////////////////////

CLL *CLL_init(CLL *lst) { return lst->lnk[0]=lst->lnk[1]=lst; }

void CLL_release(CLL *lst,void (*cll_release)(CLL *lnk))
{
    CLL *cll;
    while (cll=CLL_get(lst,1,0)) cll_release(cll);
}

CLL *CLL_put(CLL *lst,CLL *lnk,int end)
{
    if (!lst || !lnk) return NULL;
    lnk->lnk[end]=lst->lnk[end];
    lnk->lnk[!end]=lst;
    lnk->lnk[end]->lnk[!end]=lst->lnk[end]=lnk;
    return lnk;
}

CLL *CLL_pop(CLL *lnk)
{
    if (!lnk) return NULL;
    lnk->lnk[0]->lnk[1]=lnk->lnk[1];
    lnk->lnk[1]->lnk[0]=lnk->lnk[0];
    lnk->lnk[0]=lnk->lnk[1]=NULL;
    return lnk;
}

CLL *CLL_get(CLL *lst,int pop,int end)
{
    if (!lst || lst==lst->lnk[end]) return NULL;
    return pop?CLL_pop(lst->lnk[end]):lst->lnk[end];
}

void *CLL_traverse(CLL *lst,int reverse,CLL_OP op,void *data)
{
    CLL *result,*lnk=lst->lnk[reverse];
    while (lnk && lnk!=lst && !(result=op(lnk,data)))
        lnk=lnk->lnk[reverse];
    return result;
}

#define CLL_EMPTY(lst) (CLL_get(lst,NULL,0,0))


//////////////////////////////////////////////////
// LisTree
//////////////////////////////////////////////////

RBR *RBR_init(RBR *rbr)
{
    RB_EMPTY_ROOT(rbr);
    return rbr;
}

void RBR_release(RBR *rbr,void (*rbn_release)(RBN *rbn))
{
    RBN *rbn;
    while (rbn=rbr->rb_node)
    {
        rb_erase(rbn,rbr);
        rbn_release(rbn);
    }
}

void *RBR_traverse(RBR *rbr,LT_OP op,void *data)
{
    RBN *result,*rbn=rb_first(rbr);
    while (rbn && !(result=op(rbn,data))) rbn=rb_next(rbn);
    return result;
}


// get a new LTV and prepare for insertion
LTV *LTV_new(void *data,int len,int flags)
{
    LTV *ltv=NULL;
    if (data &&
        ((ltv=(LTV *) CLL_get(&ltv_repo,1,1)) || (ltv=NEW(LTV))))
    {
        ltv->len=len<0?strlen((char *) data):len;
        ltv->flags=flags;
        ltv->data=flags&LT_DUP?bufdup(data,ltv->len):data;
    }
    return ltv;
}

void LTV_free(LTV *ltv)
{
    ZERO(*ltv);
    CLL_put(&ltv_repo,&ltv->repo[0],0);
}


// get a new LTVR
LTVR *LTVR_new()
{
    LTVR *ltvr=(LTVR *) CLL_get(&ltvr_repo,1,1);
    if (!ltvr) ltvr=NEW(LTVR);
    return ltvr;
}

void LTVR_free(LTVR *ltvr)
{
    ZERO(*ltvr);
    CLL_put(&ltvr_repo,&ltvr->repo[0],0);
}


// get a new LTI and prepare for insertion
LTI *LTI_new(char *name,int len)
{
    LTI *lti;
    if (name &&
        ((lti=(LTI *) CLL_get(&lti_repo,1,1)) || (lti=NEW(LTI))))
    lti->name=len<0?strdup(name):strndup(name,len);
    CLL_init(&lti->cll);
    return lti;
}

void LTI_free(LTI *lti)
{
    ZERO(*lti);
    CLL_put(&lti_repo,&lti->repo[0],0);
}


int LT_strcmp(char *name,int len,char *lti_name)
{
    int result;
    if (len==-1)
    {
        result=strcmp(name,lti_name);
    }
    else
    {
        result=strncmp(name,lti_name,len);
        if (!result && name[len]) result=-1; // handle substring match/string mismatch.
    }
    return result;
}

// return node that owns "name", inserting if desired AND required.
LTI *LT_lookup(RBR *rbr,char *name,int len,int insert)
{
    LTI *lti=NULL;
    if (rbr && name)
    {
        RBN **rbn = &(rbr->rb_node);

        while (*rbn)
        {
            int result = LT_strcmp(name,len,((LTI *) *rbn)->name);
            if (!result) return (LTI *) *rbn; // found it!
            else rbn=(result<0)? &(*rbn)->rb_left:&(*rbn)->rb_right;
        }
        if (insert && (lti=LTI_new(name,len)))
        {
            rb_link_node(&lti->rbn,*rbn?rb_parent(*rbn):NULL,rbn); // add
            rb_insert_color(&lti->rbn,rbr); // rebalance
        }
    }
    return lti;
}


//////////////////////////////////////////////////
// Tag Team of release methods for LT elements
//////////////////////////////////////////////////

void LTV_release(LTV *ltv)
{
    if (ltv && ltv->refs--<=1)
    {
        RBR_release(&ltv->rbr,LTI_release);
        if (ltv->flags&LT_DUP) DELETE(ltv->data);
        LTV_free(ltv);
    }
}

void LTVR_release(CLL *cll)
{
    LTVR *ltvr=(LTVR *) cll;
    if (ltvr)
    {
        LTV_release(ltvr->ltv);
        LTVR_free(ltvr);
    }
}

void LTI_release(RBN *rbn)
{
    LTI *lti=(LTI *) rbn;
    if (lti)
    {
        CLL_release(&lti->cll,LTVR_release);
        DELETE(lti->name);
        LTI_free(lti);
    }
}


//////////////////////////////////////////////////
// Basic LT insert/remove
//////////////////////////////////////////////////

LTV *LTV_put(CLL *cll,LTV *ltv,int end)
{
    LTV *rval=NULL;
    LTVR *ltvr;
    TRY(!(cll && ltv && (ltvr=LTVR_new())),NULL,done,"cll/ltv/ltvr:0x%x/0x%x/0x%x\n",cll,ltv,ltvr);
    TRY(!CLL_put(cll,(CLL *) ltvr,end),NULL,done,"CLL_put(...) failed!\n",0);
    rval=ltvr->ltv=ltv;
    rval->refs++;
 done:
    return rval;
}

LTV *LTV_get(CLL *cll,int pop,int end)
{
    LTV *rval=NULL;
    LTVR *ltvr=NULL;
    TRY(!(cll && (ltvr=(LTVR *) CLL_get(cll,pop,end))),NULL,done,"cll/ltvr: 0x%x/0x%x\n",cll,ltvr);
    rval=ltvr->ltv;
    rval->refs--;
    if (pop) LTVR_free(ltvr);
 done:
    return rval;
}

void LT_init()
{
    CLL_init(&ltv_repo);
    CLL_init(&ltvr_repo);
    CLL_init(&lti_repo);
}

LTV *LT_put(RBR *rbr,LTV *ltv,char *name,int len,int end)
{
    LTV *rval=NULL;
    LTI *lti;
    TRY(!(rbr && ltv && name),NULL,done,"rbr/ltv/name: %p,%p,%p\n",rbr,ltv,name);
    TRY(!(lti=LT_lookup(rbr,name,len,1)),NULL,done,"LT_lookup failed\n",0);
    TRY(!(rval=LTV_put(&lti->cll,ltv,end)),NULL,done,"edict_enlist_ltv(...) failed\n",0);
 done:
    return rval;
}

LTV *LT_get(RBR *rbr,char *name,int len,int pop,int end)
{
    LTV *rval=NULL;
    LTI *lti;
    TRY(!(rbr && name),NULL,done,"rbr/name: %p,%p\n",rbr,name);
    if (lti=LT_lookup(rbr,name,len,0))
    {
        rval=LTV_get(&lti->cll,pop,end);
        if (pop && !CLL_get(&lti->cll,0,0))
        {
            rb_erase(&lti->rbn,rbr);
            LTI_release(&lti->rbn);
        }
    }
 done:
    return rval;
}



