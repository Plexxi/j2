/*
 * j2 - A simple concatenative programming language that can understand
 *      the structure of and dynamically link with compatible C binaries.
 *
 * Copyright (C) 2011 Jason Nyberg <jasonnyberg@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#define _GNU_SOURCE
#define _C99
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

#include <dlfcn.h>
#include <arpa/inet.h>

#include "util.h"
#include "cll.h"
#include "listree.h"
#include "reflect.h"
#include "vm.h"
#include "compile.h"

sem_t vm_process_access,vm_process_escapement;
pthread_rwlock_t vm_rwlock = PTHREAD_RWLOCK_INITIALIZER;

__attribute__((constructor))
static void init(void)
{
    sem_init(&vm_process_access,1,0); // allows first wait through
    sem_init(&vm_process_escapement,0,0); // blocks on first wait
}

char *res_name[] = { "dict","code","refs","ip","wip" };

//////////////////////////////////////////////////
// Processor
//////////////////////////////////////////////////
int vm_env_init(VM_ENV *env)
{
    int status=0;
    ZERO(*env);
    LTV_init(&env->lnk,"env0",-1,LT_NONE);
    env->stackref=LTV_init(NEW(LTV),"$",-1,LT_RO);
    STRY(!REF_create(env->stackref),"creating stackref");
    for (int res=0;res<VMRES_COUNT;res++)
        CLL_init(&env->res[res]);
 done:
    return status;
}

int vm_env_release(VM_ENV *env)
{
    env->stackref->flags&=~LT_RO;
    LTV_release(env->stackref);
    for (int res=0;res<VMRES_COUNT;res++) {
        CLL_release(&env->res[res],LTVR_release);
    }
}

int vm_env_enq(LTV *envs,VM_ENV *env)
{
    int status=0;
    STRY(!LTV_enq(LTV_list(envs),&env->lnk,TAIL),"enqueing process env");
    sem_post(&vm_process_escapement);
 done:
    return status;
}

VM_ENV *vm_env_deq(LTV *envs)
{
    sem_wait(&vm_process_escapement);
    return (VM_ENV *) LTV_deq(LTV_list(envs),HEAD);
}

void *vm_thunk(void *udata)
{
    LTV *envs=(LTV *) udata;
    VM_ENV *env=NULL;
    while ((env=vm_env_deq(envs)) && !vm_eval(env) && !vm_env_enq(envs,env));
 done:
    return env && env->state?NON_NULL:NULL;
}

//////////////////////////////////////////////////
// Environment
//////////////////////////////////////////////////
LTV *vm_enq(VM_ENV *env,int res,LTV *tos) { return LTV_put(&env->res[res],tos,HEAD,NULL); }
LTV *vm_deq(VM_ENV *env,int res,int pop) { return LTV_get(&env->res[res],pop,HEAD,NULL,NULL); }

int vm_dump(VM_ENV *env)
{
    int status=0;
    char filename[256];
    for (int i=0;i<VMRES_COUNT;i++)
    {
        printf("\n%s:\n",res_name[i]);
        print_ltvs(stdout,CODE_RED,&env->res[i],CODE_RESET "\n",0);
        sprintf(filename,"/tmp/vm_%s.dot",res_name[i]);
        graph_ltvs_to_file(filename,&env->res[i],5,res_name[i]);
    }
    return status;
}

int vm_ref_hres(CLL *cll,LTV *ref)
{
    int status=0;
    LTV *resolve_ltv(LTV *dict) { return REF_resolve(dict,ref,FALSE)?NULL:REF_ltv(REF_HEAD(ref)); }
    void *op(CLL *lnk) { return resolve_ltv(((LTVR *) lnk)->ltv); }
    STRY(!CLL_map(cll,FWD,op),"resolving ref");
 done:
    return status;
}

int vm_stack_enq(VM_ENV *env,LTV *ltv)
{
    int status=0;
    STRY(REF_resolve(vm_deq(env,VMRES_DICT,KEEP),env->stackref,TRUE),"resolving stack");
    STRY(REF_assign(REF_HEAD(env->stackref),ltv),"xfer wip to stack");
 done:
    return status;
}

int vm_stack_deq(VM_ENV *env,int pop)
{
    int status=0;
    LTV *ltv=NULL;
    STRY(vm_ref_hres(&env->res[VMRES_DICT],env->stackref),"resolving stack");
    STRY(!vm_enq(env,VMRES_WIP,REF_ltv(REF_HEAD(env->stackref))),"xfer tos to wip");
    if (pop)
        STRY(REF_remove(REF_HEAD(env->stackref)),"removing tos");
 done:
    return status;
}

int vm_context_push(VM_ENV *env,LTV *ltv)
{
    int status=0;
    STRY(!vm_enq(env,VMRES_DICT,ltv),"pushing dict level");
 done:
    return status;
}

int vm_context_pop(VM_ENV *env)
{
    int status=0;
    LTV *context=NULL;
    LTI *oldstack=NULL,*newstack=NULL;
    STRY(!(context=vm_deq(env,VMRES_DICT,POP)),"popping context");
    STRY(REF_resolve(context,env->stackref,FALSE),"resolving old stack");
    STRY(!(oldstack=REF_lti(REF_HEAD(env->stackref))),"retreiving old stack");
    STRY(REF_resolve(vm_deq(env,VMRES_DICT,KEEP),env->stackref,FALSE),"resolving new stack");
    STRY(!(newstack=REF_lti(REF_HEAD(env->stackref))),"retreiving new stack");
    CLL_MERGE(&newstack->ltvs,&oldstack->ltvs,HEAD);
    STRY(vm_stack_enq(env,context),"returning dict context to stack");
 done:
    return status;
}

int vm_lambda_push(VM_ENV *env,LTV *ltv)
{
    int status=0;
    STRY(!vm_enq(env,VMRES_CODE,ltv),"pushing code");
    STRY(!vm_enq(env,VMRES_IP,LTV_ZERO),"pushing IP");
 done:
    return status;
}

int vm_lambda_pop(VM_ENV *env)
{
    int status=0;
    LTV_release(vm_deq(env,VMRES_CODE,POP));
    LTV_release(vm_deq(env,VMRES_IP,POP));
 done:
    return status;
}

int builtin(VM_ENV *env)
{
    printf("builtin:\n");
    LTV *ref=vm_deq(env,VMRES_REFS,POP);
    if (!strncmp("dump",ref->data,ref->len))
        vm_dump(env);
    else if (!strncmp("ref",ref->data,ref->len))
        vm_stack_enq(env,ref_mod);
    else if (!strncmp("mod",ref->data,ref->len))
        dump_module_simple("/tmp/module.dot",ref_mod);
    LTV_release(ref);
}

int vm_tos(VM_ENV *env)
{
    int status=0;
    STRY(REF_resolve(vm_deq(env,VMRES_DICT,KEEP),env->stackref,TRUE),"resolving stack");
    REF *ref=REF_HEAD(env->stackref);
    LTI *lti=REF_lti(ref);
    print_ltvs(stdout,CODE_RED,&lti->ltvs,CODE_RESET "\n",0);
    graph_ltvs_to_file("/tmp/vm_tos.dot",&lti->ltvs,0,"TOS");
 done:
    return status;
}


//////////////////////////////////////////////////
// Bytecode Interpreter
//////////////////////////////////////////////////
int vm_eval(VM_ENV *env)
{
    int status=0;

    LTV *code_ltv=NULL,*ip_ltv=NULL;
    STRY(!env,"validating environment");
    if (!(code_ltv=vm_deq(env,VMRES_CODE,KEEP)) || !(ip_ltv=vm_deq(env,VMRES_IP,KEEP))) {
        status=!0;
        goto done;
    }

    char *data=(char *) code_ltv->data;
    int len=code_ltv->len;
    int *ip=(int *) &ip_ltv->data;

    LTV *decode_extended() {
        unsigned length,flags;
        VM_BC_LTV *extended=NULL;
        extended=(VM_BC_LTV *) (data+(*ip));
        length=ntohl(extended->length);
        flags=ntohl(extended->flags);
        (*ip)+=(sizeof(length)+sizeof(flags)+length);
        return LTV_init(NEW(LTV),extended->data,length,flags);
    }

    unsigned char op=0,res=0;;

#define OPCODE(vmop) case (unsigned char) vmop: printf(CODE_RED "0x%x\n" CODE_RESET,(unsigned char) vmop);

    while ((*ip)<len && data[*ip]) {
        op=data[(*ip)++];
        switch(op) {
            OPCODE(VMOP_RES_DICT)  res=0; break;
            OPCODE(VMOP_RES_CODE)  res=1; break;
            OPCODE(VMOP_RES_REFS)  res=2; break;
            OPCODE(VMOP_RES_IP)    res=3; break;
            OPCODE(VMOP_RES_WIP)   res=4; break;

            OPCODE(VMOP_SPUSH)     STRY(vm_stack_enq(env,vm_deq(env,VMRES_WIP,POP)),"SPUSH'ing"); break;
            OPCODE(VMOP_SPOP)      STRY(vm_stack_deq(env,POP),"SPOP'ing"); break;
            OPCODE(VMOP_SPEEK)     STRY(vm_stack_deq(env,KEEP),"SPEEK'ing"); break;

            OPCODE(VMOP_PUSH)      vm_enq(env,res,vm_deq(env,VMRES_WIP,POP)); break;
            OPCODE(VMOP_POP)       vm_enq(env,VMRES_WIP,vm_deq(env,res,POP)); break;
            OPCODE(VMOP_PEEK)      vm_enq(env,VMRES_WIP,vm_deq(env,res,KEEP)); break;
            OPCODE(VMOP_DUP)       vm_enq(env,res,vm_deq(env,res,KEEP)); break;
            OPCODE(VMOP_DROP)      LTV_release(vm_deq(env,res,POP)); break;

            OPCODE(VMOP_LIT)       vm_enq(env,VMRES_WIP,decode_extended()); break;
            OPCODE(VMOP_BUILTIN)   builtin(env); break;
            OPCODE(VMOP_TOS)       vm_tos(env); break;
            OPCODE(VMOP_YIELD)     goto done; // break out of loop, requeue env;

            OPCODE(VMOP_REF_MAKE)  STRY(!vm_enq(env,VMRES_REFS,REF_create(vm_deq(env,VMRES_WIP,POP))),"making a ref"); break;
            OPCODE(VMOP_REF_INS)   STRY(REF_resolve(vm_deq(env,VMRES_DICT,KEEP),vm_deq(env,VMRES_REFS,KEEP),TRUE),"inserting ref"); break;
            OPCODE(VMOP_REF_RES)   STRY(REF_resolve(vm_deq(env,VMRES_DICT,KEEP),vm_deq(env,VMRES_REFS,KEEP),FALSE),"resolving ref"); break;
            OPCODE(VMOP_REF_HRES)  STRY(vm_ref_hres(&env->res[VMRES_DICT],vm_deq(env,VMRES_REFS,KEEP)),"hierarchically resolving ref"); break;
            OPCODE(VMOP_REF_ITER)  STRY(REF_iterate(vm_deq(env,VMRES_REFS,KEEP),KEEP),"iterating ref"); break;
            OPCODE(VMOP_DEREF)     STRY(!vm_enq(env,VMRES_WIP,REF_ltv(REF_HEAD(vm_deq(env,VMRES_REFS,KEEP)))),"dereferencing"); break;
            OPCODE(VMOP_ASSIGN)    STRY(REF_assign(REF_HEAD(vm_deq(env,VMRES_REFS,KEEP)),vm_deq(env,VMRES_WIP,POP)),"assigning to ref"); break;
            OPCODE(VMOP_REMOVE)    STRY(REF_remove(REF_HEAD(vm_deq(env,VMRES_REFS,KEEP))),"removing from ref"); break;
            OPCODE(VMOP_THROW)     break;
            OPCODE(VMOP_CATCH)     break;
            OPCODE(VMOP_MAP)       break;
            OPCODE(VMOP_APPEND)    break;
            OPCODE(VMOP_COMPARE)   break;
            OPCODE(VMOP_RDLOCK)    pthread_rwlock_rdlock(&vm_rwlock); break;
            OPCODE(VMOP_WRLOCK)    pthread_rwlock_wrlock(&vm_rwlock); break;
            OPCODE(VMOP_UNLOCK)    pthread_rwlock_unlock(&vm_rwlock); break;

            OPCODE(VMOP_EDICT)     vm_lambda_push(env,compile_ltv(compilers[FORMAT_edict],  vm_deq(env,VMRES_WIP,POP))); goto done;
            OPCODE(VMOP_XML)       vm_lambda_push(env,compile_ltv(compilers[FORMAT_xml],    vm_deq(env,VMRES_WIP,POP))); goto done;
            OPCODE(VMOP_JSON)      vm_lambda_push(env,compile_ltv(compilers[FORMAT_json],   vm_deq(env,VMRES_WIP,POP))); goto done;
            OPCODE(VMOP_YAML)      vm_lambda_push(env,compile_ltv(compilers[FORMAT_yaml],   vm_deq(env,VMRES_WIP,POP))); goto done;
            OPCODE(VMOP_SWAGGER)   vm_lambda_push(env,compile_ltv(compilers[FORMAT_swagger],vm_deq(env,VMRES_WIP,POP))); goto done;
            OPCODE(VMOP_LISP)      vm_lambda_push(env,compile_ltv(compilers[FORMAT_lisp],   vm_deq(env,VMRES_WIP,POP))); goto done;
            OPCODE(VMOP_MASSOC)    vm_lambda_push(env,compile_ltv(compilers[FORMAT_massoc], vm_deq(env,VMRES_WIP,POP))); goto done;
/*
  case VMOP_REF_ITER_KEEP: REF_iterate(LTV_list(vm_deq(env,VMRES_REFS,KEEP)),KEEP); break;
  case VMOP_REF_ITER_POP: REF_iterate(LTV_list(vm_deq(env,VMRES_REFS,KEEP)),POP); break;
  case VMOP_GRAPH_STACK: graph_ltvs_to_file("/tmp/jj.dot",LTV_list(vm_deq(env,VMRES_STACK,KEEP)),0,"tos"); break;
  case VMOP_PRINT_REFS:  REF_printall(stdout,LTV_list(vm_deq(env,VMRES_REFS,KEEP)),"Ref: "); break;
  case VMOP_GRAPH_REFS:  REF_dot(stdout,LTV_list(vm_deq(env,VMRES_REFS,KEEP)),"Ref: "); break;
*/

            default: STRY((env->state=ENV_BROKEN),"evaluating invalid bytecode 0x%x",op); break;;
        }
    }
    vm_lambda_pop(env);

 done:
    return status;
}

int vm_thread(LTV *env,LTV *code)
{
}

int vm_init(int argc,char *argv[])
{
    int status=0;
    LTV *dict=LTV_init(NEW(LTV),"DICT",-1,LT_RO|LT_NONE);
    LTV *envs=LTV_init(NEW(LTV),"ENVS",-1,LT_RO|LT_LIST);
    LT_put(dict,"ENVS",HEAD,envs);

    VM_ENV *env=NEW(VM_ENV);
    vm_env_init(env); // initial env is dict-root+bootstrap-code
    STRY(vm_context_push(env,dict),"pushing initial env context");

////////////////////////////////////////////////////////////////////////////////////////////////////

    int format=FORMAT_edict;
    FILE *file=stdin;
    char *data;
    int len;
    LTV *ltv=NULL;

    switch (argc)
    {
        case 3:
            file=fopen(argv[2],"r");
            // FALL THRU!
        case 2:
            for (int format=0;format<FORMAT_MAX;format++)
                if (!strcmp(formats[format],argv[1]))
                    break;
            break;
        case 1:
            printf("Specify format (asm | edict | xml | json | yaml | mmassoc)\n");
            break;
    }

    do {
        TRYCATCH((data=balanced_readline(file,&len))==NULL,0,close_file,"reading balanced line from file");
        TRYCATCH(!(ltv=compile(compilers[format],data,len)),TRY_ERR,free_data,"compiling balanced line");
        print_ltv(stdout,"bytecodes: ",ltv,"\n",0);
        TRYCATCH(vm_lambda_push(env,ltv),TRY_ERR,free_data,"pushing initial env code");
        vm_env_enq(envs,env);
        vm_thunk(envs);
    free_data:
        DELETE(data);
    } while(1);

 close_file:
    if (file!=stdin)
        fclose(file);

 release_env:
    printf("releasing env\n");
    vm_env_release(env);

////////////////////////////////////////////////////////////////////////////////////////////////////

 done:
    return status;
}
