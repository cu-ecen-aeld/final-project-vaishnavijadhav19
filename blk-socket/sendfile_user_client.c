//sendfile_user_client
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

#define TCP_PORT 9000
#define TCP_SERVER_IP "10.0.2.15"  // <-- change to RPI2’s IP

#define BUF_SIZE 4096
#define MAX_FILES 1024
#define MAX_WORKERS 4               // adjust to taste
#define DEV_PATH "/dev/compblk0"
//#define DEV_PATH "/tmp/test_output.bin"

//#define htobe64(x) __bswap_64(x)

//#ifndef htobe64
//static inline uint64_t htobe64(uint64_t host_64bits)
//{
//#if __BYTE_ORDER == __LITTLE_ENDIAN
//    return __builtin_bswap64(host_64bits);
//#else
//    return host_64bits;
//#endif
//}
//#endif


#define RPI1
//#define RPI2
typedef struct {
    char **names;  //filenames
    int count;// total number of files
    int next_idx;// file index to process file
    pthread_mutex_t idx_mtx; // mutex
} work_queue_t;

static pthread_mutex_t dev_mtx = PTHREAD_MUTEX_INITIALIZER; 
static pthread_mutex_t net_mtx = PTHREAD_MUTEX_INITIALIZER;


static int write_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, p + off, len - off, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
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
    size_t off = 0;
    while (off < len) {
        ssize_t n = recv(fd, p + off, len - off, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recv");
            return -1;
        } else if (n == 0) {
            // connection closed early
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

#ifdef RPI1
static int send_file_tcp(const char *fname)
{
    // 1. Open file and get size
    FILE *fp = fopen(fname, "rb");
    if (!fp) {
        perror("fopen send_file_tcp");
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(fp);
        return -1;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        perror("ftell");
        fclose(fp);
        return -1;
    }
    rewind(fp);
    size_t filesize = (size_t)sz;

    // 2. Create socket and connect to RPI2
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("socket");
        fclose(fp);
        return -1;
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(TCP_PORT);
    if (inet_pton(AF_INET, TCP_SERVER_IP, &sa.sin_addr) != 1) {
        perror("inet_pton");
        close(s);
        fclose(fp);
        return -1;
    }

    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("connect");
        close(s);
        fclose(fp);
        return -1;
    }

    uint32_t name_len = (uint32_t)strlen(fname);
    uint32_t name_len_net = htonl(name_len);
    if (write_all(s, &name_len_net, sizeof(name_len_net)) < 0) {
        close(s);
        fclose(fp);
        return -1;
    }

    if (write_all(s, fname, name_len) < 0) {
        close(s);
        fclose(fp);
        return -1;
    }

    uint64_t fs_net = htobe64((uint64_t)filesize); // use htobe64, or implement manually if missing
    if (write_all(s, &fs_net, sizeof(fs_net)) < 0) {
        close(s);
        fclose(fp);
        return -1;
    }

    unsigned char buffer[BUF_SIZE];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (write_all(s, buffer, n) < 0) {
            close(s);
            fclose(fp);
            return -1;
        }
    }
    if (ferror(fp)) {
        perror("fread in send_file_tcp");
        close(s);
        fclose(fp);
        return -1;
    }

    printf("Sent file %s (%zu bytes) over TCP\n", fname, filesize);

    close(s);
    fclose(fp);
    return 0;
}
#endif

static int compute_sha256_file(const char *filename, unsigned char out[32]) 
{
    FILE *file = fopen(filename, "rb");
    if (!file) 
    { 
    	perror("fopen"); 
    	return -1; 
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) 
    { 
    	fclose(file); 
    	return -1; 
    }
    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL) != 1) 
    { 
    	EVP_MD_CTX_free(mdctx); 
    	fclose(file); 
    	return -1; 
    }

    unsigned char buf[BUF_SIZE]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, file)) > 0) 
    {
        if (EVP_DigestUpdate(mdctx, buf, n) != 1) 
        { 
        	EVP_MD_CTX_free(mdctx); 	
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
	printf("%02x", h[i]); 
	printf("\n"); 
}

static int ends_with_ci(const char *s, const char *ext) 
{
    size_t ls=strlen(s), le=strlen(ext); //ls = length of filename, le= length of file extension
    const char *a=s+(ls-le);
    for (size_t i=0;i<le;i++)
    {
    	 if (tolower((unsigned char)a[i])!=tolower((unsigned char)ext[i]))  //ignoring upper/lowercase .jpg/.JPG will behave same for both
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
        size_t base_len=(size_t)(dot-in);
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
    FILE *fp=fopen(src_path,"rb"); 
    if(!fp)
    { 
    	perror("fopen src"); 
    	return -1; 
    }
    int fd=open(dev_path,O_WRONLY|O_CREAT|O_TRUNC,0644); 
    if(fd<0)
    { 
    	perror("open dev"); 
    	fclose(fp); 
    	return -1; 
    }
    unsigned char buffer[BUF_SIZE]; 
    size_t n;
    while((n=fread(buffer,1,sizeof buffer,fp))>0)
    {
        size_t w=0; 
        while(w<n)
        { 
        	ssize_t r=write(fd, buffer+w, n-w);
            	if(r<0)
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
    if(ferror(fp))
    { 
    	perror("fread src"); 
    	close(fd); 
    	fclose(fp); 
    	return -1; 
    }
    (void)fsync(fd); 
    close(fd); 
    fclose(fp); 
    return 0;
}
/*static int read_dev_to_file(const char *dev_path, const char *dst_path) 
{
    int fd=open(dev_path,O_RDONLY); 
    if(fd<0)
    {
    	perror("open dev read"); 
    	return -1; 
    }
    FILE *out=fopen(dst_path,"wb"); 
    if(!out)
    {
    	perror("fopen dst"); 
    	close(fd); 
    	return -1; 
    }
    unsigned char buffer[BUF_SIZE];
    for(;;)
    {
        ssize_t n=read(fd,buffer,sizeof buffer);
        if(n>0)
        {
        	size_t off=0; 
        	while(off<(size_t)n)
        	{ 
        		size_t w=fwrite(buffer+off,1,(size_t)n-off,out);
                	if(w==0)
                	{
                		perror("fwrite dst"); 
                		fclose(out); 
                		close(fd); 
                		return -1;
                	} 
                	off+=w; 
                	}
                }
        else if(n==0) break;
        else { if(errno==EINTR) continue; perror("read dev"); fclose(out); close(fd); return -1; }
    }
    fflush(out); 
    int outfd=fileno(out); 
    if(outfd>=0)
    {
    	 (void)fsync(outfd);
    }
    fclose(out); 
    close(fd); 
    return 0;
}*/

static size_t get_file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
        return (size_t)st.st_size;
    else
        return 0;
}


static int read_dev_to_file(const char *dev_path, const char *dst_path, size_t bytes_to_read)
{
    int fd = open(dev_path, O_RDONLY);
    if (fd < 0) 
    {
        perror("open dev read");
        return -1;
    }

    FILE *out = fopen(dst_path, "wb");
    if (!out) 
    {
        perror("fopen dst");
        close(fd);
        return -1;
    }

    unsigned char buffer[BUF_SIZE];
    size_t total_read = 0;

    while (total_read < bytes_to_read) 
    {
        size_t to_read = bytes_to_read - total_read;
        if (to_read > sizeof(buffer))
        {
            to_read = sizeof(buffer);
        }

        ssize_t n = read(fd, buffer, to_read);
        if (n > 0) 
        {
            size_t written = fwrite(buffer, 1, (size_t)n, out);
            if (written < (size_t)n) 
            {
                perror("fwrite dst");
                fclose(out);
                close(fd);
                return -1;
            }
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
    int outfd = fileno(out);
    if (outfd >= 0)
    {
        fsync(outfd);
    }

    fclose(out);
    close(fd);
    return 0;
}


static int process_one_file_threadsafe(const char *fname) 
{
    printf("[T%lu] === %s ===\n", (unsigned long)pthread_self(), fname);

    unsigned char in_hash[32];
    if (compute_sha256_file(fname, in_hash) != 0) 
    {
        fprintf(stderr, "[T%lu] SHA-256 failed on input %s\n", (unsigned long)pthread_self(), fname);
        return -1;
    }
    printf("[T%lu] src SHA-256: ", (unsigned long)pthread_self()); 
    print_hash_hex(in_hash);

    // Exclusive access to DEV_PATH for write->read transaction
    pthread_mutex_lock(&dev_mtx);

    if (write_file_to_dev(fname, DEV_PATH) != 0) 
    {
        pthread_mutex_unlock(&dev_mtx);
        return -1;
    }

    char rxname[1024]; 
    make_rx_name(fname, rxname, sizeof rxname);
    size_t filesize = get_file_size(fname);   // Get original file size
    
    if (filesize == 0) 
    {
	    fprintf(stderr, "Error: Could not determine file size for %s\n", fname);
	    pthread_mutex_unlock(&dev_mtx);
	    return -1;
	}

    if (read_dev_to_file(DEV_PATH, rxname, filesize) != 0) 
    {
        pthread_mutex_unlock(&dev_mtx);
        return -1;
    }

    pthread_mutex_unlock(&dev_mtx);
    
#ifdef RPI1
    // Send the rx file over TCP to RPI2 (one thread at a time)
    pthread_mutex_lock(&net_mtx);
    if (send_file_tcp(rxname) != 0) {
        fprintf(stderr, "[T%lu] TCP send failed for %s\n",
                (unsigned long)pthread_self(), rxname);
        pthread_mutex_unlock(&net_mtx);
        // You can choose to return -1 here or just continue
        // return -1;
    }
    pthread_mutex_unlock(&net_mtx);
#endif

    unsigned char out_hash[32];
    if (compute_sha256_file(rxname, out_hash) != 0) 
    {
        fprintf(stderr, "[T%lu] SHA-256 failed on output %s\n", (unsigned long)pthread_self(), rxname);
        return -1;
    }
    printf("[T%lu] rx SHA-256: ", (unsigned long)pthread_self()); print_hash_hex(out_hash);

    if (memcmp(in_hash, out_hash, 32)==0) 
    {
        printf("[T%lu] LOCAL RESULT: MATCH (%s → %s)\n", (unsigned long)pthread_self(), fname, rxname);
        return 0;
    } else {
        printf("[T%lu] LOCAL RESULT: MISMATCH (%s)\n", (unsigned long)pthread_self(), fname);
        return -1;
    }
}

// -------- worker threads --------
static void *worker(void *arg) 
{
    work_queue_t *q = (work_queue_t *)arg;
    for (;;) 
    {
        int idx = -1;
        pthread_mutex_lock(&q->idx_mtx);
        if (q->next_idx < q->count) 
        {
        	idx = q->next_idx++;
        }
        pthread_mutex_unlock(&q->idx_mtx);

        if (idx < 0) 
        {
        	break; // done
        }

        const char *fname = q->names[idx];
        (void)process_one_file_threadsafe(fname);
    }
    return NULL;
}


#ifdef RPI1
int main(void) 
{
    char *names[MAX_FILES]; //Allocating array of pointers to char
    int nfiles = 0;
    DIR *d = opendir(".");  //opens current directory
    if (!d) 
    { 
    	perror("opendir"); 
    	return 1; 
    }
    for (;;) 
    {
        struct dirent *e = readdir(d); //reads each entry
        if (!e) // if there is no entry left will return NULL
        {
        	break;
        }
   
        if (ends_with_ci(e->d_name,".txt") || ends_with_ci(e->d_name,".jpg") || ends_with_ci(e->d_name,".jpeg")) 
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

    if (nfiles == 0) //If o file is present in current directory
    { 
    	printf("No .txt/.jpg/.jpeg files found.\n"); 
    	return 0; 
    }
    printf("Found %d files. Starting up to %d workers…\n", nfiles, MAX_WORKERS);

    work_queue_t q = { 
    .names = names, 
    .count = nfiles, 
    .next_idx = 0, 
    .idx_mtx = PTHREAD_MUTEX_INITIALIZER 
    };

    pthread_t tids[MAX_WORKERS]; //creating 4 threads for process
    int nworkers = (nfiles < MAX_WORKERS) ? nfiles : MAX_WORKERS; // if nfiles available in current directory are greater that4 Max 4 threads will get created
    for (int i=0;i<nworkers;i++) 
    {
    	pthread_create(&tids[i], NULL, worker, &q);
    }
    for (int i=0;i<nworkers;i++)
    {
    	 pthread_join(tids[i], NULL);
    }

    for (int i=0;i<nfiles;i++) 
    {
    	free(names[i]);
    }
    return 0;
}

#elif defined(RPI2)

// === RPI2 main: TCP server that receives files and prints SHA ===
static int handle_one_connection(int cfd)
{
    // 1. Read name length
    uint32_t name_len_net;
    if (read_all(cfd, &name_len_net, sizeof(name_len_net)) < 0)
        return -1;
    uint32_t name_len = ntohl(name_len_net);
    if (name_len == 0 || name_len > 1023) {
        fprintf(stderr, "Bad name_len: %u\n", name_len);
        return -1;
    }

    // 2. Read filename
    char fname[1024];
    if (read_all(cfd, fname, name_len) < 0)
        return -1;
    fname[name_len] = '\0';

    // 3. Read filesize
    uint64_t fs_net;
    if (read_all(cfd, &fs_net, sizeof(fs_net)) < 0)
        return -1;
    uint64_t filesize = be64toh(fs_net);

    // 4. Save as, e.g., "rpi2_<originalname>"
    char outname[1200];
    snprintf(outname, sizeof(outname), "rpi2_%s", fname);

    FILE *out = fopen(outname, "wb");
    if (!out) {
        perror("fopen out");
        return -1;
    }

    unsigned char buffer[BUF_SIZE];
    uint64_t remaining = filesize;
    while (remaining > 0) {
        size_t to_read = (remaining > BUF_SIZE) ? BUF_SIZE : (size_t)remaining;
        ssize_t n = recv(cfd, buffer, to_read, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recv data");
            fclose(out);
            return -1;
        } else if (n == 0) {
            fprintf(stderr, "Connection closed early\n");
            fclose(out);
            return -1;
        }
        size_t w = fwrite(buffer, 1, (size_t)n, out);
        if (w < (size_t)n) {
            perror("fwrite out");
            fclose(out);
            return -1;
        }
        remaining -= (uint64_t)n;
    }

    fflush(out);
    int outfd = fileno(out);
    if (outfd >= 0)
        fsync(outfd);
    fclose(out);

    // 5. Compute SHA on received file
    unsigned char hash[32];
    if (compute_sha256_file(outname, hash) != 0) {
        fprintf(stderr, "SHA-256 failed on received file %s\n", outname);
        return -1;
    }
    printf("Received file %s (%llu bytes), SHA-256: ",
           outname, (unsigned long long)filesize);
    print_hash_hex(hash);

    return 0;
}

int main(void)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
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

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(s);
        return 1;
    }

    if (listen(s, 5) < 0) {
        perror("listen");
        close(s);
        return 1;
    }

    printf("RPI2 TCP server listening on port %d\n", TCP_PORT);

    for (;;) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int cfd = accept(s, (struct sockaddr *)&peer, &plen);
        if (cfd < 0) {
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

#endif // RPI2
