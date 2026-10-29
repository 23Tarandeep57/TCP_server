#include "tcpserver.h"
static int16 id_seq = 0;

static bool make_path(char *out, size_t sz, const char* dir, const char* fname) {
	int n;
	n = snprintf(out, sz, "%s/%s", dir, fname);
	return n>=0 && (size_t)n<sz;
}

static bool his_path(char* out, size_t sz, const char *a, const char *b) {
	char fname[2*UNAME_LEN+16];
	int n;
	size_t fsz = sizeof(fname);
	if (strcmp(a, b) < 0)
		n = snprintf(fname, fsz, "%s__%s.jsonl", a, b);
	else 
		n = snprintf(fname, fsz, "%s__%s.jsonl", b, a);
	if (n < 0 || (size_t)n >= fsz) return false;
	return make_path(out, sz, HISTORY_DIR, fname);
}

static bool pending_path(char* out, size_t sz, const char* uname) {
	char fname[UNAME_LEN + 16];
	int n;
	size_t fsz = sizeof(fname);
	n = snprintf(fname, fsz, "%s.jsonl", uname);
	if (n < 0 || (size_t)n >= fsz) return false;
	return make_path(out, sz, PENDING_DIR, fname);
}

static int64 new_id() {
	return ((int64)time(NULL) <<20) | (++id_seq & ((1ULL <<20) -1));
}

static bool append_json(FILE * fp, const cJSON *obj) {
	char *json;
	bool ok;

	json = cJSON_PrintUnformatted(obj);
	if (!json) return false;
	
	ok = (fprintf(fp, "%s\n", json) >= 0);
	free(json);
	if (ok && fflush(fp) != 0) ok = false;
	return ok;
} 

static cJSON *make_msg(int64 id, const char *sender, const char* getter, const char *text) {
	cJSON *obj;
	obj = cJSON_CreateObject();

	if (!obj) return NULL;

	cJSON_AddNumberToObject(obj, "id", (double)id);
	cJSON_AddStringToObject(obj, "from", sender);
	cJSON_AddStringToObject(obj, "to", getter);
	cJSON_AddStringToObject(obj, "text", text);
    return obj;
}

bool msg_init() {
	if (mkdir(DATA_DIR, 0700) < 0 && errno != EEXIST)
		return false;
	if (mkdir(HISTORY_DIR, 0700) < 0 && errno != EEXIST)
		return false;
	if (mkdir(PENDING_DIR, 0700) < 0 && errno != EEXIST)
		return false;
	return true;
}

bool store_msg(const char* sender, const char *getter, const char* text, int64 *msg_id) {
	printf("%d , %d, %s\n", valid_user(sender), valid_user(getter), text);
	printf("%s", sender);
	if (valid_user(sender) == -1 || valid_user(getter) == -1 || !text || !msg_id) 
		return false;

	int64 id;
	char hpath[256];
	char ppath[256];
	cJSON *obj;
	FILE *fptr;
	bool ok;
	printf("0000\n");
	if(!his_path(hpath, sizeof(hpath), sender, getter) || !pending_path(ppath, sizeof(ppath), getter))
		return false;
	
	printf("1\n");
	id = new_id();
	obj = make_msg(id, sender, getter, text);
	if (!obj) return false;
	printf("2\n");
	fptr = fopen(ppath, "a");
	if (!fptr) {
		cJSON_Delete(obj);
		return  false;
	}
	printf("3\n");
	ok = append_json(fptr, obj);

	if (!ok) {
		fclose(fptr);
		cJSON_Delete(obj);
		return  false;
	}
	printf("4\n");
	if (fclose(fptr) != 0) {
		cJSON_Delete(obj);
		return false;
	}

	fptr = fopen(hpath, "a");
	if (!fptr) {
		cJSON_Delete(obj);
		return false;
	}
	printf("5\n");
	ok = append_json(fptr, obj);	
	if (fclose(fptr) != 0) ok = false;
	cJSON_Delete(obj);
	
	printf("6\n");
	if (!ok) return false;
	*msg_id = id;
	return true;
}

static bool sendall(int fd, const char *buf, size_t len) {
	size_t send= 0;
	while(send < len) {
		ssize_t n = write(fd, buf + send, len-send);
		if (n > 0) {
			send += (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR) continue;
		if (n < 0 && (errno == EAGAIN || errno==EWOULDBLOCK)){
			int epfd = epoll_create1(EPOLL_CLOEXEC);
			if (epfd < 0) return false;

			struct epoll_event event = {
				.events = EPOLLOUT,
				.data.fd = fd
			};
			if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event) < 0) {
				close(epfd);
				return false;
			}

			int ret;
			do {
				ret = epoll_wait(epfd, &event, 1, 1000);
			} while (ret < 0 && errno == EINTR);
			close(epfd);

			if (ret > 0 && (event.events & EPOLLOUT) &&
			    !(event.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)))
				continue;
			return false;
		}
		return false;
	}
	return true;
}


bool deliver_one(int fd, int64 id,const char *sender, const char *getter, const char *text) {
    cJSON *obj = make_msg(id, sender, getter, text);
    if (!obj) return false;

    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!json) return false;

    size_t len = strlen(json);
    char *wire = malloc(len + 6);
    if (!wire) {
        free(json);
        return false;
    }

    memcpy(wire, "MSG ", 4);
    memcpy(wire + 4, json, len);
    wire[len + 4] = '\n';

    bool ok = sendall(fd, wire, len + 5);

    free(wire);
    free(json);

    return ok;
}

bool deliver_pending(const char* uname, int fd) {
	if (valid_user(uname) == -1) return false;

	char path[256];
	FILE *fptr;
	char* line;
	size_t cap, len;
	bool ok;
	printf("1\n");
	ok = true;
	cap = 0;

	if (!pending_path(path, sizeof(path), uname))
		return false;
	
	printf("2\n");
	fptr = fopen(path, "r");
	if (!fptr) return errno == ENOENT;
	
	while(getline(&line, &cap, fptr) != -1) {
		len = strlen(line);
		if (!sendall(fd, "MSG", 4) || !sendall(fd, line, len)) {
			ok = false;
			break;
		}
	}	printf("3\n");
	if (ferror(fptr)) ok = false;
	printf("4\n");
	free(line);
	fclose(fptr);
	return ok;
	
}
bool ack_msg(const char *uname, int64 msg_id){
	if (valid_user(uname) == -1) return false;

	char path[256];
	char temp[280];
	int n;
	FILE *in, *out;
	char* line;
	size_t cap;
	bool rm, ok, match;

	if (!pending_path(path, sizeof(path), uname))
		return false;

	n = snprintf(temp, sizeof(temp), "%s.tmp", path);
	if (n < 0 || (size_t)n >= sizeof(temp))
		return false;

	in = fopen(path, "r");
	if (!in) return errno == ENOENT;
	
	out = fopen(temp, "w");
	if (!out) {
		fclose(in);
		return false;
	}

	cap = 0;
	rm = false;
	ok = true;

	while(getline(&line, &cap, in) != -1) {
		cJSON *obj = cJSON_Parse(line);
		const cJSON *id = obj ? cJSON_GetObjectItemCaseSensitive(obj, "id") : NULL;
		
		match = id && cJSON_IsNumber(id) && ((int64)id->valuedouble == msg_id);

		if (match && !rm) rm = true;
		else if (fputs(line, out) == EOF) {
			ok = false;
			cJSON_Delete(obj);
			break;
		}
		cJSON_Delete(obj);
	}

	if (ferror(in)) ok = false;
	free(line);

	if (fclose(in) != 0) ok = false;
	if (fclose(out) != 0) ok = false;

	if (!ok || !rm) {
		unlink(temp);
		return ok && !rm;
	}

	if (rename(temp, path) != 0) {
		unlink(temp);
		return false;
	}
	return true;
}
