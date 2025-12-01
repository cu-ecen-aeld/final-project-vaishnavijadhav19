/*
	sendfile_user_server.c
	Author - Anuja Joshi
*/


#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <endian.h>
#include <byteswap.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include "compblk_uapi.h"

//#define RPI1
#define RPI2

#define TCP_PORT 9000

#ifdef RPI1 
#define TCP_SERVER_IP "10.0.0.167"  // <--RPI IP
//#define TCP_SERVER_IP "10.0.2.15"//ubuntu testing
#endif
#ifdef RPI2
#define TCP_SERVER_IP "10.0.0.167"  // <-- RPI IP
//#define TCP_SERVER_IP "10.0.2.15"//ubuntu testing
#endif

#define BUF_SIZE 4096
#define MAX_FILES 1024
#define MAX_WORKERS 4 //number of threads

#define DEV_PATH "/dev/compblk0"
//#define DEV_PATH "/tmp/test_output.bin"

#define COMPBLK_LOGICAL_BLK_SIZE 4096

static void print_hash_hex(const unsigned char h[32]);

typedef struct {
    char **names; //file names
    int count; //number of filenames in names[]
    int next_idx; //next idx of file
    pthread_mutex_t idx_mtx;
} work_queue_t;

static pthread_mutex_t dev_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t net_mtx = PTHREAD_MUTEX_INITIALIZER;

static int write_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = buf;
    size_t off = 0; //how many bytes are already sent
    while (off < len) 
    {
        ssize_t n = send(fd, p + off, len - off, 0); //sending remaining bytes
        if (n < 0) 
        {
            if (errno == EINTR) 
            {
            	continue;
            }
            perror("send");
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static int read_all(int fd, void *buf, size_t len)
{
    unsigned char *p = buf;
    size_t off = 0; //bytes read till now
    while (off < len) 
    {
        ssize_t n = recv(fd, p + off, len - off, 0); //try to read remaining bytes
        if (n < 0) 
        {
            if (errno == EINTR) 
            {
            	continue;
            }
            perror("recv");
            return -1;
        } 
        else if (n == 0) 
        {
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

#ifdef RPI1
static int send_file_tcp_no_sha(const char *fname)
{
    FILE *fp = fopen(fname, "rb");
    if (!fp) 
    {
    	perror("fopen send_file_tcp_no_sha"); 
    	return -1; 
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);

    int s = socket(AF_INET, SOCK_STREAM, 0); //Create TCP socket
    if (s < 0) 
    { 
    	perror("socket"); 
    	fclose(fp); 
    	return -1; 
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET; //IPv4
    sa.sin_port = htons(TCP_PORT);
    inet_pton(AF_INET, TCP_SERVER_IP, &sa.sin_addr); //convert IP string to binary

    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) 
    {
        perror("connect");
        close(s);
        fclose(fp);
        return -1;
    }

    uint32_t name_len = (uint32_t)strlen(fname);
    uint32_t name_len_net = htonl(name_len);
    write_all(s, &name_len_net, sizeof(name_len_net)); //send filname length
    write_all(s, fname, name_len); //send filename

    uint64_t fs_net = htobe64((uint64_t)sz);
    write_all(s, &fs_net, sizeof(fs_net)); //send filesize

    unsigned char buffer[BUF_SIZE];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) 
    {
        write_all(s, buffer, n); //send file
    }

    close(s);
    fclose(fp);
    return 0;
}


static int send_file_tcp_with_sha(const char *fname, unsigned char out_sha[32])
{
    FILE *fp = fopen(fname, "rb");
    if (!fp) 
    {
    	 perror("fopen send_file_tcp_with_sha"); 
    	 return -1; 
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);

    int s = socket(AF_INET, SOCK_STREAM, 0);  //Create TCP socket
    if (s < 0) 
    { 
    	perror("socket"); 
    	fclose(fp); 
    	return -1; 
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET; //IPv4
    sa.sin_port = htons(TCP_PORT);
    inet_pton(AF_INET, TCP_SERVER_IP, &sa.sin_addr);

    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) 
    {
        perror("connect");
        close(s);
        fclose(fp);
        return -1;
    }

    uint32_t name_len = (uint32_t)strlen(fname);
    uint32_t name_len_net = htonl(name_len);
    write_all(s, &name_len_net, sizeof(name_len_net)); //send filename length
    write_all(s, fname, name_len); //send filename

    uint64_t fs_net = htobe64((uint64_t)sz);
    write_all(s, &fs_net, sizeof(fs_net)); //send file size

    unsigned char buffer[BUF_SIZE];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) 
    {
        write_all(s, buffer, n); //send file
    }

    if (read_all(s, out_sha, 32) != 0) 
    {
        perror("read_all sha");
    }

    close(s);
    fclose(fp);
    return 0;
}
#endif

//COMPUTE SHA
static int compute_sha256_file(const char *filename, unsigned char out[32]) 
{
    FILE *file = fopen(filename, "rb");
    if (!file) 
    { 
    	perror("fopen"); 
    	return -1; 
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new(); //allocate new hash
    if (!mdctx) 
    { 
    	fclose(file); 
    	return -1; 
    }

    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL) != 1) //Initialization of hash to use SHA256
    {
        EVP_MD_CTX_free(mdctx); 
        fclose(file); 
        return -1;
    }

    unsigned char buf[BUF_SIZE]; 
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, file)) > 0) 
    {
        if (EVP_DigestUpdate(mdctx, buf, n) != 1) //update hash
        {
            EVP_MD_CTX_free(mdctx); //free hash
            fclose(file); 
            return -1;
        }
    }

    unsigned int dummy = 0;
    if (EVP_DigestFinal_ex(mdctx, out, &dummy) != 1) 
    {
        EVP_MD_CTX_free(mdctx); 
        fclose(file); 
        return -1;
    }
    EVP_MD_CTX_free(mdctx);
    fclose(file); 
    return 0;
}
static void print_hash_hex(const unsigned char h[32]) 
{ 
    for (int i=0;i<32;i++) 
    {
        printf("%02x", h[i]); 
    }
    printf("\n"); 
}

static int ends_with_ci(const char *s, const char *ext)  
{
    size_t ls=strlen(s), le=strlen(ext); //ls = length of filename, le = length of extension
    if (ls < le) 
    {
    	return 0;
    }
    const char *a=s+(ls-le); //pointer pointing to extension start
    for (size_t i=0;i<le;i++) 
    {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)ext[i])) //case insensitive check
        {
            return 0;
        }
    }
    return 1;
}

static void make_rx_name(const char *in, char *out, size_t cap) 
{
    const char *dot = strrchr(in, '.');
    if (dot && dot!=in) 
    {
        size_t base_len=(size_t)(dot-in); // ength before dot
        if (base_len+3+strlen(dot)+1 >= cap) 
        {
            snprintf(out,cap,"%s_rx",in); 
            return; 
        }
        memcpy(out,in,base_len); 
        memcpy(out+base_len,"_rx",3); 
        strcpy(out+base_len+3,dot);
    } 
    else 
    {
        snprintf(out,cap,"%s_rx",in);
    }
}

static int write_file_to_dev(const char *src_path, const char *dev_path) 
{
    FILE *fp=fopen(src_path,"rb"); //open source file
    if (!fp) 
    { 
    	perror("fopen src"); 
    	return -1; 
    }
    int fd = open(dev_path,O_WRONLY|O_CREAT|O_TRUNC,0644); //open device for writing
    if (fd < 0) 
    {
    	perror("open dev"); 
    	fclose(fp); 
    	return -1; 
    }
    unsigned char buffer[BUF_SIZE]; 
    size_t n;
    while ((n=fread(buffer,1,sizeof buffer,fp))>0) //read from source file into buffer
    {
        size_t w=0; 
        while (w<n) 
        { 
            ssize_t r=write(fd, buffer+w, n-w);
            if (r<0) 
            { 
                if(errno==EINTR) 
                {
                	continue; 
                }
                perror("write dev"); 
                close(fd); 
                fclose(fp); 
                return -1; 
            } 
            w+=(size_t)r; 
        }
    }
    if (ferror(fp)) 
    { 
        perror("fread src"); 
        close(fd); 
        fclose(fp); 
        return -1; 
    }
    fsync(fd); 
    close(fd); 
    fclose(fp); 
    return 0;
}

static size_t get_file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
    {
        return (size_t)st.st_size;
    }
    else
    {
        return 0;
    }
}


static int export_compressed_to_file(const char *dev_path, const char *out_path, size_t orig_size)
{
    int fd = open(dev_path, O_RDONLY); //open device for reading
    if (fd < 0) 
    {
        perror("open dev for ioctl");
        return -1;
    }

    FILE *out = fopen(out_path, "wb"); //open output file to write compressed data
    if (!out) 
    {
        perror("fopen compressed out");
        close(fd);
        return -1;
    }

    size_t num_blocks = (orig_size + COMPBLK_LOGICAL_BLK_SIZE - 1) / COMPBLK_LOGICAL_BLK_SIZE;
    if (num_blocks == 0) num_blocks = 1;

    for (uint32_t bi = 0; bi < num_blocks; ++bi) 
    {
        struct compblk_ioctl_blockdump dump;
        memset(&dump, 0, sizeof(dump));
        dump.bi = bi;

        if (ioctl(fd, COMPBLK_IOCTL_DUMP_STORED, &dump) < 0) 
        {
            perror("ioctl DUMP_STORED");
            fclose(out);
            close(fd);
            return -1;
        }

        if (!dump.valid) 
        {
            fprintf(stderr, "block %u invalid; stopping\n", bi);
            break;
        }
        if (dump.stored_len == 0) 
        {
            fprintf(stderr, "block %u stored_len=0; stopping\n", bi);
            break;
        }
        size_t written = fwrite(dump.data, 1, dump.stored_len, out); //Write compressed bytes into the output file
        if (written < dump.stored_len) 
        {
            perror("fwrite compressed out");
            fclose(out);
            close(fd);
            return -1;
        }
    }

    fflush(out);
    if (fileno(out) >= 0) 
    {
    	fsync(fileno(out));
    }
    fclose(out);
    close(fd);
    return 0;
}

static int export_metadata_to_file(const char *dev_path, const char *meta_path, size_t orig_size)
{
    FILE *fp = fopen(meta_path, "w"); //open metadata output file
    if (!fp) 
    {
        perror("fopen metadata");
        return -1;
    }

    int fd = open(dev_path, O_RDONLY); //open device for reading
    if (fd < 0) 
    {
        perror("open dev for metadata");
        fclose(fp);
        return -1;
    }

    size_t num_blocks = (orig_size + COMPBLK_LOGICAL_BLK_SIZE - 1) / COMPBLK_LOGICAL_BLK_SIZE; //Calculate how many logical blocks are there
    if (num_blocks == 0) 
    {
    	num_blocks = 1;
    }
    
    fprintf(fp, "orig_size=%zu\n", orig_size);
    fprintf(fp, "num_blocks=%zu\n\n", num_blocks);

    for (uint32_t bi = 0; bi < num_blocks; bi++) 
    {
        struct compblk_ioctl_map_entry e;
        memset(&e, 0, sizeof(e));
        e.bi = bi;

        if (ioctl(fd, COMPBLK_IOCTL_GET_MAP_ENTRY, &e) < 0) 
        {
            perror("ioctl GET_MAP_ENTRY");
            fclose(fp);
            close(fd);
            return -1;
        }

        fprintf(fp, "block=%u\n""offset=%u\n""length=%u\n""flags=%u\n""valid=%u\n\n", bi, e.off, e.len, e.flags, e.valid); //fields stored to text file, one block at a time
    }

    fclose(fp);
    close(fd);
    return 0;
}

static int read_dev_to_file(const char *dev_path, const char *dst_path, size_t bytes_to_read)
{
    int fd = open(dev_path, O_RDONLY);  //Open device for reading
    if (fd < 0) 
    {
        perror("open dev read");
        return -1;
    }

    FILE *out = fopen(dst_path, "wb"); //Open output file for writing
    if (!out) 
    {
        perror("fopen dst");
        close(fd);
        return -1;
    }

    unsigned char buffer[BUF_SIZE];
    size_t total_read = 0; //how many bytes read so far
    while (total_read < bytes_to_read) 
    {
        size_t to_read = bytes_to_read - total_read;
        if (to_read > sizeof(buffer)) to_read = sizeof(buffer);

        ssize_t n = read(fd, buffer, to_read); //Read from device
        if (n > 0) 
        {
            fwrite(buffer, 1, (size_t)n, out); //write to output file
            total_read += (size_t)n;
        } 
        else if (n == 0) 
        {
            break;
        } 
        else if (errno == EINTR) 
        {
            continue;
        } 
        else 
        {
            perror("read dev");
            fclose(out);
            close(fd);
            return -1;
        }
    }

    fflush(out);
    fsync(fileno(out));
    fclose(out);
    close(fd);
    return 0;
}

static int process_one_file_threadsafe(const char *fname)
{
    printf("[T%lu] === %s ===\n", (unsigned long)pthread_self(), fname); // Print thread ID and filename

    unsigned char in_hash[32];
    if (compute_sha256_file(fname, in_hash) != 0) 
    {
        fprintf(stderr, "[T%lu] SHA-256 failed on input %s\n", (unsigned long)pthread_self(), fname);
        return -1;
    }
    printf("[T%lu] src SHA-256: ", (unsigned long)pthread_self()); 
    print_hash_hex(in_hash);

    pthread_mutex_lock(&dev_mtx);

    if (write_file_to_dev(fname, DEV_PATH) != 0)  //Copy original file into compblk device
    {
        pthread_mutex_unlock(&dev_mtx);
        return -1;
    }

    char rxname[1024];
    make_rx_name(fname, rxname, sizeof rxname); //rx filename after compression

    size_t filesize = get_file_size(fname);
    if (filesize == 0) 
    {
        fprintf(stderr, "Error: Could not determine file size for %s\n", fname);
        pthread_mutex_unlock(&dev_mtx);
        return -1;
    }


    if (export_compressed_to_file(DEV_PATH, rxname, filesize) != 0) 
    {
        fprintf(stderr, "export compressed to file failed for %s\n", fname);
        pthread_mutex_unlock(&dev_mtx);
        return -1;
    }

    char meta_file[1024];
    snprintf(meta_file, sizeof meta_file, "%s.meta", fname); // Create metadata filename
    if (export_metadata_to_file(DEV_PATH, meta_file, filesize) != 0) 
    {
        fprintf(stderr, "export_metadata to file failed for %s\n", fname);
        pthread_mutex_unlock(&dev_mtx);
        return -1;
    }

    char out_final[1024];
    snprintf(out_final, sizeof out_final, "%s_out", fname);
    if (read_dev_to_file(DEV_PATH, out_final, filesize) != 0) // Read decompressed data from the device to file
    {
        fprintf(stderr, "read dev to file failed for %s\n", fname);
        pthread_mutex_unlock(&dev_mtx);
        return -1;
    }


    unsigned char out_hash[32];
    if (compute_sha256_file(out_final, out_hash) != 0) 
    {
        fprintf(stderr, "[T%lu] SHA-256 failed on decompressed %s_out\n", (unsigned long)pthread_self(), fname);
        pthread_mutex_unlock(&dev_mtx);
        return -1;
    }

    printf("[T%lu] decompressed SHA-256 (local): ",(unsigned long)pthread_self()); //Print decompressed file hash
    print_hash_hex(out_hash);

    if (memcmp(in_hash, out_hash, 32) == 0) 
    {
        printf("[T%lu] FILE MATCH: %s == %s_out\n", (unsigned long)pthread_self(), fname, out_final); //Compare original vs decompressed hash 
    } 
    else 
    {
        printf("[T%lu] FILE MISMATCH: %s vs %s_out\n", (unsigned long)pthread_self(), fname, out_final);
    }

    pthread_mutex_unlock(&dev_mtx);

#ifdef RPI1
    // Send compressed file (no SHA)
    pthread_mutex_lock(&net_mtx);
    send_file_tcp_no_sha(rxname);

    // Send metadata file (expect SHA back)
    /*unsigned char sha_from_rpi2[32];
    send_file_tcp_with_sha(meta_file, sha_from_rpi2);
    pthread_mutex_unlock(&net_mtx);

    printf("[T%lu] RPI2 returned SHA: ", (unsigned long)pthread_self());
    print_hash_hex(sha_from_rpi2);

    if (memcmp(in_hash, sha_from_rpi2, 32) == 0) {
        printf("[T%lu] REMOTE MATCH: original %s OK\n", (unsigned long)pthread_self(), fname);
    } else {
        printf("[T%lu] REMOTE MISMATCH: original %s NOT OK\n", (unsigned long)pthread_self(), fname);
    }*/
#endif

    return 0;
}

static void *worker(void *arg)
{
    work_queue_t *q = (work_queue_t *)arg;
    for (;;) 
    {
        int idx = -1;
        
        pthread_mutex_lock(&q->idx_mtx); //Lock index mutex to safely update next_idx
        if (q->next_idx < q->count) 
        {
            idx = q->next_idx++;
        }
        pthread_mutex_unlock(&q->idx_mtx);

        if (idx < 0) 
        {
        	break;
        }

        const char *fname = q->names[idx]; //get filename for this index
        process_one_file_threadsafe(fname);
    }
    return NULL;
}

#ifdef RPI1
int main(void)
{
    char *names[MAX_FILES];
    int nfiles = 0;
    DIR *d = opendir(".");
    if (!d) {
        perror("opendir");
        return 1;
    }
    while (1) 
    {
        struct dirent *e = readdir(d);
        if (!e) break;
        if (ends_with_ci(e->d_name, ".txt") || ends_with_ci(e->d_name, ".jpg") || ends_with_ci(e->d_name, ".jpeg")) 
        {
            names[nfiles] = strdup(e->d_name);
            if (!names[nfiles]) 
            {
                perror("strdup");
                closedir(d);
                return 1;
            }
            if (++nfiles >= MAX_FILES) 
            {
            	break;
            }
        }
    }
    closedir(d);

    if (nfiles == 0) 
    {
        printf("No input files.\n");
        return 0;
    }

    work_queue_t q = {
        .names = names,
        .count = nfiles,
        .next_idx = 0,
        .idx_mtx = PTHREAD_MUTEX_INITIALIZER
    };

    pthread_t tids[MAX_WORKERS];
    int nworkers = (nfiles < MAX_WORKERS) ? nfiles : MAX_WORKERS;

    for (int i = 0; i < nworkers; i++)
    {
        pthread_create(&tids[i], NULL, worker, &q);
    }

    for (int i = 0; i < nworkers; i++)
    {
        pthread_join(tids[i], NULL);
    }

    for (int i = 0; i < nfiles; i++)
    {
        free(names[i]);
    }

    return 0;
}

#elif defined(RPI2)
static int handle_one_connection(int cfd)
{
    // 1. Read name length
    uint32_t name_len_net;
    if (read_all(cfd, &name_len_net, sizeof(name_len_net)) < 0)
    {
        return -1;
    }
    uint32_t name_len = ntohl(name_len_net);
    if (name_len == 0 || name_len > 1023) 
    {
        fprintf(stderr, "Bad name_len: %u\n", name_len);
        return -1;
    }

    // 2. Read filename
    char fname[1024];
    if (read_all(cfd, fname, name_len) < 0)
    {
        return -1;
    }
    fname[name_len] = '\0';

    // 3. Read filesize
    uint64_t fs_net;
    if (read_all(cfd, &fs_net, sizeof(fs_net)) < 0)
    {
        return -1;
    }
    uint64_t filesize = be64toh(fs_net);

    // 4. Save as "rpi2_originalname"
    char outname[1200];
    snprintf(outname, sizeof(outname), "rpi2_%s", fname);

    FILE *out = fopen(outname, "wb");
    if (!out) 
    {
        perror("fopen out");
        return -1;
    }

    unsigned char buffer[BUF_SIZE];
    uint64_t remaining = filesize;
    while (remaining > 0) 
    {
        size_t to_read = (remaining > BUF_SIZE) ? BUF_SIZE : (size_t)remaining;
        ssize_t n = recv(cfd, buffer, to_read, 0);
        if (n < 0) 
        {
            if (errno == EINTR) 
            {
            	continue;
            }
            perror("recv data");
            fclose(out);
            return -1;
        } 
        else if (n == 0) 
        {
            fprintf(stderr, "Connection closed early\n");
            fclose(out);
            return -1;
        }
        size_t w = fwrite(buffer, 1, (size_t)n, out);
        if (w < (size_t)n) 
        {
            perror("fwrite out");
            fclose(out);
            return -1;
        }
        remaining -= (uint64_t)n;
    }

    fflush(out);
    int outfd = fileno(out);
    if (outfd >= 0)
    {
        fsync(outfd);
    }
    fclose(out);

    // 5. Compute SHA on received file  
    unsigned char hash[32];
    if (compute_sha256_file(outname, hash) != 0) 
    {
        fprintf(stderr, "SHA-256 failed on received file %s\n", outname);
        return -1;
    }
    printf("Received file %s (%llu bytes), SHA-256: ", outname, (unsigned long long)filesize);
    print_hash_hex(hash);
    if (write_all(cfd, hash, 32) < 0) 
    {
    	fprintf(stderr, "Failed to send SHA256 back to client\n");
    } 

    return 0;
}

int main(void)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) 
    {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TCP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) 
    {
        perror("bind");
        close(s);
        return 1;
    }

    if (listen(s, 5) < 0) 
    {
        perror("listen");
        close(s);
        return 1;
    }

    printf("RPI2 TCP server listening on port %d\n", TCP_PORT);

    for (;;) 
    {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int cfd = accept(s, (struct sockaddr *)&peer, &plen);
        if (cfd < 0) 
        {
            perror("accept");
            continue;
        }

        // Handle exactly one file per connection
        handle_one_connection(cfd);
        close(cfd);
    }

    close(s);
    return 0;
}

#endif
