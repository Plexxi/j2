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

#include <dlfcn.h>
#include <arpa/inet.h>

#include "util.h"
#include "compile.h"
#include "vm.h"

char *formats[] = {"asm","edict","xml","json","yaml","lisp","massoc"};

int jit_term(char *type,char *data,int len)
{
    printf("%s, \"",type);
    fstrnprint(stdout,data,len);
    printf("\"\n");
}

#define VMOP_XFER(src,dst) ((VM_CMD) {0xc0|(((src)&7)<<3)|((dst)&7)})

int jit_asm(EMITTER emit,void *data,int len)
{
    void *end=data+len;
    VM_CMD *cmd=data;
    for (int i=0;(void *)(cmd+i)<end && cmd[i].op;i++) {

        emit(cmd+i);
    }
}

#define EDICT_OPS "|&!%#@/+="
#define EDICT_MONO_OPS "()<>{}"

#define EMIT(bc) emit(&((VM_CMD) {VMOP_ ## bc}))

int jit_edict(EMITTER emit,void *data,int len)
{
    int status=0;
    char *tdata=(char *) data;
    int tlen=0;

    int advance(int adv) { adv=MIN(adv,len); tdata+=adv; len-=adv; return adv; }

    int compile_ws() {
        tlen=series(tdata,len,WHITESPACE,NULL,NULL);
        return advance(tlen);
    }

    int compile_lit() {
        if ((tlen=series(tdata,len,NULL,NULL,"[]")))
            emit(&((VM_CMD) {VMOP_LIT,tlen-2,LT_DUP,tdata+1}));
        return advance(tlen);
    }

    int compile_atom() {
        if ((tlen=series(tdata,len,EDICT_MONO_OPS,NULL,NULL))) { // special, non-ganging op
            jit_term("edict/block",tdata,tlen);
            switch (*tdata) {
                case '<': emit(&VMOP_XFER(VMRES_STACK,VMRES_DICT)); break;
                case '>': emit(&VMOP_XFER(VMRES_DICT,VMRES_STACK)); break;
                case '(': emit(&VMOP_XFER(VMRES_STACK,VMRES_DICT)); break;
                case ')':
                    emit(&VMOP_XFER(VMRES_DICT,VMRES_STACK));
                    emit(&((VM_CMD) {VMOP_EDICT})); break;
                case '{': emit(&VMOP_XFER(VMRES_STACK,VMRES_DICT)); break;
                case '}': emit(&VMOP_XFER(VMRES_DICT,VMRES_STACK)); break;
            }
            advance(tlen);
        } else {
            char *ops_data=tdata;
            int ops_len=series(tdata,len,EDICT_OPS,NULL,NULL);
            advance(ops_len);
            int ref_len=series(tdata,len,NULL,WHITESPACE EDICT_OPS EDICT_MONO_OPS,"[]");
            if (ref_len)
                emit(&((VM_CMD) {VMOP_REF,ref_len,LT_DUP,tdata}));
            if (ops_len) {
                for (int i=0;i<ops_len;i++) {
                    switch (ops_data[i]) {
                        case '#': emit(&((VM_CMD) {VMOP_BUILTIN})); break;
                        case '@': emit(&((VM_CMD) {VMOP_ASSIGN}));  break;
                        case '/': emit(&((VM_CMD) {VMOP_REMOVE}));  break;
                        case '!': EMIT(EDICT); EMIT(YIELD);         break;
                        case '&': emit(&((VM_CMD) {VMOP_THROW}));   break;
                        case '|': emit(&((VM_CMD) {VMOP_CATCH}));   break;
                        case '%': emit(&((VM_CMD) {VMOP_MAP}));     break;
                        case '+': emit(&((VM_CMD) {VMOP_APPEND}));  break;
                        case '=': emit(&((VM_CMD) {VMOP_COMPARE})); break;
                    }
                }
                if (ref_len)
                    EMIT(REF_DEQ);
            } else
                emit(&((VM_CMD) {VMOP_DEREF}));

            tlen=ops_len+advance(ref_len);
        }
        return tlen;
    }

    STRY(!tdata,"testing compiler source");
    while (len && (compile_ws() || compile_lit() || compile_atom()));

 done:
    return status;
}

int jit_xml(EMITTER emit,void *data,int len)    { printf("unsupported\n"); }
int jit_json(EMITTER emit,void *data,int len)   { printf("unsupported\n"); }
int jit_yaml(EMITTER emit,void *data,int len)   { printf("unsupported\n"); }
int jit_lisp(EMITTER emit,void *data,int len)   { printf("unsupported\n"); }
int jit_massoc(EMITTER emit,void *data,int len) { printf("unsupported\n"); }


COMPILER compilers[] = {jit_asm,jit_edict,jit_xml,jit_json,jit_yaml,jit_lisp,jit_massoc};


////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

LTV *compile(COMPILER compiler,void *data,int len)
{
    char *buf=NULL;
    size_t flen=0;
    FILE *stream=open_memstream(&buf,&flen);

    int emit(VM_CMD *cmd) {
        unsigned unsigned_val;
        fwrite(&cmd->op,1,1,stream);
        switch (cmd->len) {
            case 0: break;
            case -1:
                cmd->len=strlen(cmd->data); // rewrite len and...
                // ...fall thru!
            default:
                unsigned_val=htonl(cmd->len);
                fwrite(&unsigned_val,sizeof(unsigned_val),1,stream);
                unsigned_val=htonl(cmd->flags);
                fwrite(&unsigned_val,sizeof(unsigned_val),1,stream);
                fwrite(cmd->data,1,cmd->len,stream);
                break;
        }
    }

    compiler(emit,data,len);
    fclose(stream);
    return LTV_init(NEW(LTV),buf,flen,LT_OWN|LT_BIN);
}

LTV *compile_ltv(COMPILER compiler,LTV *ltv)
{
    LTV *bc=compile(compiler,ltv->data,ltv->len);
    LTV_release(ltv);
    return bc;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
