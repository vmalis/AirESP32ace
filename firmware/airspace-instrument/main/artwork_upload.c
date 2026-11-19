// AirESP32ace
// Created by VM, November 2026
#include "artwork_upload.h"
#include "storage_policy.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Serialised by the single paired HTTP server. .anw and .aol are reserved
 * siblings of allowlisted artwork files; path is never client-supplied. */
enum { ART_PATH = 104, ART_CHUNK = 1536, ART_MAX = 466*466*3 };
static bool siblings(const char *path, char stage[ART_PATH], char old[ART_PATH])
{
    const char *ext = strrchr(path,'.');
    if (!ext || (strcmp(ext,".rgb") && strcmp(ext,".rga")) || strlen(path)>=ART_PATH) return false;
    size_t n = (size_t)(ext-path);
    memcpy(stage,path,n); memcpy(old,path,n);
    memcpy(stage+n,".anw",5); memcpy(old+n,".aol",5);
    return true;
}
static int exists(const char *path)
{
    struct stat st;
    if (stat(path,&st)==0) {
        if (S_ISREG(st.st_mode)) return 1;
        errno=EISDIR; return -1;
    }
    return errno==ENOENT ? 0 : -1;
}
bool app_artwork_required(size_t size, uint32_t cluster, uint64_t *needed)
{
    if (!needed || !size || size>ART_MAX || !cluster || (cluster&(cluster-1)) || cluster>131072) return false;
    /* The old file cannot be reclaimed until a complete new copy exists.
     * Allow one extra cluster for directory growth and protect state writes. */
    *needed=(((uint64_t)size+cluster-1)/cluster+1)*cluster+APP_STORAGE_STATE_RESERVE;
    return true;
}
app_art_result_t app_artwork_recover(const char *path)
{
    char stage[ART_PATH],old[ART_PATH];
    if (!siblings(path,stage,old)) return ART_BAD_CONTENT;
    int b=exists(old),n=exists(stage);
    if (b<0 || n<0) return ART_MEDIA_IO;
    if (!b && !n) return ART_OK;
    int live=exists(path);
    if (live<0) return ART_MEDIA_IO;
    if (b) {
        /* The success ACK is issued only after the backup is removed. If
         * reset during either rename, roll back the unacknowledged update. */
        if (live && n) return ART_CONFLICT; /* ambiguous: preserve all three */
        if (live && rename(path,stage)!=0) return ART_MEDIA_IO;
        n |= live;
        if (rename(old,path)!=0) return ART_MEDIA_IO;
    }
    if (n && unlink(stage)!=0) return ART_MEDIA_IO;
    return ART_OK;
}
static uint32_t hash_more(uint32_t hash, const uint8_t *b, size_t n)
{
    for (size_t i=0;i<n;++i) hash=(hash^b[i])*16777619U;
    return hash;
}
/* Verify the same bytes through the filesystem, not just the receive buffer. */
static int file_matches(const char *path, size_t size, uint32_t hash, uint8_t *scratch)
{
    struct stat st;
    if (stat(path,&st)<0) return errno==ENOENT ? 0 : -1;
    if (!S_ISREG(st.st_mode)) { errno=EISDIR; return -1; }
    if (st.st_size!=(off_t)size) return 0;
    int fd=open(path,O_RDONLY);
    if (fd<0) return -1;
    uint32_t actual=2166136261U;
    size_t total=0;
    while (total<size) {
        size_t want=size-total > ART_CHUNK ? ART_CHUNK : size-total;
        ssize_t n=read(fd,scratch,want);
        if (n<=0) { int why=n<0 ? errno:EIO; close(fd); errno=why; return -1; }
        total+=(size_t)n; actual=hash_more(actual,scratch,(size_t)n);
    }
    if (close(fd)<0) return -1;
    return hash==actual;
}
static app_art_result_t available(const app_art_io_t *io, size_t total, size_t written, app_art_report_t *r)
{
    uint32_t cluster=0;
    if (io->capacity(io->ctx,&r->free_bytes,&cluster)!=0 || !app_artwork_required(total,cluster,&r->needed_bytes)) {
        r->sys_errno=errno ? errno : EIO; return ART_VOLUME_UNAVAILABLE;
    }
    r->needed_bytes-=((written+cluster-1)/cluster)*(uint64_t)cluster;
    if (r->free_bytes<r->needed_bytes) { r->sys_errno=ENOSPC; return ART_NO_SPACE; }
    return ART_OK;
}
app_art_result_t app_artwork_install(const char *path,size_t size,bool known_hash,
    uint32_t expected,const app_art_io_t *io,app_art_report_t *r)
{
    char stage[ART_PATH],old[ART_PATH]; uint8_t scratch[ART_CHUNK];
    memset(r,0,sizeof(*r));
    if (!io || !io->capacity || !io->receive || !size || size>ART_MAX || !siblings(path,stage,old)) return ART_BAD_CONTENT;
    if (io->lock) io->lock(io->ctx);
    app_art_result_t rc=app_artwork_recover(path);
    if (io->unlock) io->unlock(io->ctx);
    if (rc!=ART_OK) { r->sys_errno=errno; return rc; }
    int same=known_hash ? file_matches(path,size,expected,scratch) : 0;
    if (same<0) { r->sys_errno=errno; return ART_MEDIA_IO; }
    int fd=-1; bool created=false;
    if (!same) {
        rc=available(io,size,0,r);
        if (rc!=ART_OK) return rc;
        fd=open(stage,O_WRONLY|O_CREAT|O_EXCL,0600);
        if (fd<0) { r->sys_errno=errno; return errno==ENOSPC ? ART_NO_SPACE : ART_MEDIA_IO; }
        created=true;
    }
    uint32_t actual=2166136261U;
    size_t next_proof=64*1024;
    while (r->received<size) {
        size_t left=size-r->received,want=left>ART_CHUNK?ART_CHUNK:left;
        if (fd>=0 && r->received>=next_proof) {
            rc=available(io,size,r->received,r);
            if (rc!=ART_OK) goto end;
            next_proof=r->received+64*1024;
        }
        int n=io->receive(io->ctx,scratch,want);
        if (n<=0 || (size_t)n>want) { rc=n==-ETIMEDOUT ? ART_TIMEOUT : ART_DISCONNECTED; goto end; }
        if (fd>=0) {
            errno=0;
            if (write(fd,scratch,(size_t)n)!=n) {
                r->sys_errno=errno ? errno : EIO; rc=r->sys_errno==ENOSPC ? ART_NO_SPACE : ART_MEDIA_IO; goto end;
            }
        }
        actual=hash_more(actual,scratch,(size_t)n); r->received+=(size_t)n;
    }
    if (known_hash && actual!=expected) { rc=ART_BAD_CONTENT; goto end; }
    if (same) return ART_OK;
    if (fsync(fd)<0) { r->sys_errno=errno; rc=errno==ENOSPC ? ART_NO_SPACE : ART_MEDIA_IO; goto end; }
    if (close(fd)<0) { fd=-1; r->sys_errno=errno; rc=ART_MEDIA_IO; goto end; }
    fd=-1;
    if (file_matches(stage,size,actual,scratch)!=1) { r->sys_errno=EIO; rc=ART_MEDIA_IO; goto end; }
    /* No cross-volume rename or rename-over-existing assumptions on FAT. */
    if (io->lock) io->lock(io->ctx);
    const int live=exists(path);
    if (live<0 || (live && rename(path,old)<0)) { r->sys_errno=errno; rc=ART_MEDIA_IO; }
    else if (rename(stage,path)<0) {
        r->sys_errno=errno; rc=ART_MEDIA_IO;
        if (live && rename(old,path)<0) { created=false; rc=ART_CONFLICT; } /* preserve evidence */
    } else {
        created=false;
        if (live && unlink(old)<0) {
            r->sys_errno=errno;
            rc=app_artwork_recover(path)==ART_OK ? ART_MEDIA_IO:ART_CONFLICT;
        }
    }
    if (io->unlock) io->unlock(io->ctx);
end:
    if (fd>=0) (void)close(fd);
    if (created && unlink(stage)<0 && errno!=ENOENT) { r->sys_errno=errno; rc=ART_CONFLICT; }
    return rc;
}
