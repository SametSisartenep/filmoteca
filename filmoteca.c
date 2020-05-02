/*
 * filmoteca - web interface
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include "libutf/utf.h"
#include "args.h"

#define nil NULL
typedef unsigned int uint;
typedef unsigned long ulong;
typedef long long vlong;
typedef unsigned long long uvlong;

enum {
	Sok		= 200,
	Spartial	= 206,
	Sbadreq		= 400,
	Sforbid		= 403,
	Snotfound	= 404,
	Snotrange	= 416,
	Sinternal	= 500,
	Snotimple	= 501,
	Swrongver	= 505,
};
char *statusmsg[] = {
 [Sok]		"OK",
 [Spartial]	"Partial Content",
 [Sbadreq]	"Bad Request",
 [Sforbid]	"Forbidden",
 [Snotfound]	"Not Found",
 [Snotrange]	"Range Not Satisfiable",
 [Sinternal]	"Internal Server Error",
 [Snotimple]	"Not Implemented",
 [Swrongver]	"HTTP Version Not Supported",
};

typedef struct Movie Movie;
typedef struct Multipart Multipart;
typedef struct Part Part;
typedef struct Series Series;
typedef struct Season Season;
typedef struct Episode Episode;
typedef struct Resource Resource;

struct Movie {
	char *release;		/* release date */
	int hassynopsis;	/* is there a synopsis, */
	int hascover;		/* cover, */
	int hasvideo;		/* video, */
	int hashistory;		/* or history file? */
	char **subs;		/* list of subtitle languages */
	int nsub;
	char **dubs;		/* list of revoicing languages */
	int ndub;
	char **extras;		/* list of extra content */
	int nextra;
	char **remakes;		/* list of remake years */
	int nremake;
};

struct Multipart {
	char *release;		/* release date */
	int hassynopsis;	/* is there a synopsis, */
	int hascover;		/* cover, */
	int hashistory;		/* or history file? */
	Part *part0;		/* list of parts */
	char **extras;		/* list of extra content */
	int nextra;
	char **remakes;		/* list of remake years */
	int nremake;
};

struct Part {
	int no;			/* part number */
	char **subs;		/* list of subtitle languages */
	int nsub;
	char **dubs;		/* list of revoicing languages */
	int ndub;
	Part *next;
};

struct Series {
	int hassynopsis;	/* is there a synopsis, */
	int hascover;		/* cover, */
	int hashistory;		/* or history file? */
	Season *s;		/* list of seasons */
	char **extras;		/* list of extra content */
	int nextra;
	char **remakes;		/* list of remake years */
	int nremake;
};

struct Season {
	char *release;		/* release date */
	int no;			/* season number */
	Episode *pilot;		/* list of episodes */
	Season *next;
};

struct Episode {
	int no;			/* episode number */
	int hasvideo;		/* is there a video file? */
	char **subs;		/* list of subtitle languages */
	int nsub;
	char **dubs;		/* list of revoicing languages */
	int ndub;
	Episode *next;
};

enum {
	Rmovie,
	Rmulti,
	Rserie,
	Runknown
};
struct Resource {
	int type;
	union {
		Movie movie;
		Multipart multi;
		Series serie;
	};
};

typedef struct Req Req;
typedef struct Res Res;
typedef struct HField HField;

struct Req {
	char *method, *target, *version;
	HField *fields;
};

struct Res {
	int status;
	HField *fields;
};

struct HField {
	char *key, *value;
	HField *next;
};

char httpver[] = "HTTP/1.1";
char srvname[] = "filmoteca";
char errmsg[] = "NO MOVIES HERE";
char listhead[] = "<!doctype html>\n<html>\n<head>\n"
	"<link rel=\"stylesheet\" href=\"/style\" media=\"all\" type=\"text/css\"/>\n"
	"<title>Filmoteca</title>\n"
	"</head>\n<body>\n"
	"<h1>Filmoteca</h1>\n";
char listfeet[] = "</body>\n</html>\n";
char portalhead[] = "<!doctype html>\n<html>\n<head>\n"
	"<link rel=\"stylesheet\" href=\"/style\" media=\"all\" type=\"text/css\"/>\n"
	"<title>Filmoteca - %s</title>\n"
	"</head>\n<body>\n<center>\n"
	"<h1>%s</h1>\n";
char portalcover[] = "<a href=\"%s/cover\"><img id=\"cover\" src=\"%s/cover\"/></a>\n";
char portalrelease[] = "<table>\n"
	"\t<tr>\n"
	"\t\t<td>Release</td><td>";
char portalmoviestream[] = "</td>\n"
	"\t</tr>\n"
	"\t<tr>\n"
	"\t\t<td>Stream</td><td><a href=\"%s/video\">link</a>";
char portalmultistream[] = "</td>\n"
	"\t</tr>\n"
	"\t<tr>\n"
	"\t\t<td>Stream</td><td>";
char portalseriestream[] = "</td>\n"
	"\t</tr>\n"
	"\t<tr>\n"
	"\t\t<td>Stream</td><td>";
char portalsynopsis[] = "</td>\n"
	"\t</tr>\n"
	"\t<tr>\n"
	"\t\t<td>Synopsis</td><td>";
char portalhistory[] = "</td>\n"
	"\t</tr>\n"
	"\t<tr>\n"
	"\t\t<td>History</td><td>";
char portalsub[] = "</td>\n"
	"\t</tr>\n"
	"\t<tr>\n"
	"\t\t<td>Subs</td><td>";
char portaldub[] = "</td>\n"
	"\t</tr>\n"
	"\t<tr>\n"
	"\t\t<td>Dubs</td><td>";
char portalextra[] = "</td>\n"
	"\t</tr>\n"
	"\t<tr>\n"
	"\t\t<td>Extras</td><td>";
char portalfeet[] = "</td>\n\t</tr>\n</table>\n</center></body>\n</html>\n";
char *assetpath = "/home/cinema/lib/film";
char *wdir = "/home/cinema/films";
Req *req;
Res *res;

void hfatal(char *);

/* a crappy mimic of the original */
void
sysfatal(char *s)
{
	perror(s);
	exit(1);
}

void *
emalloc(ulong n)
{
	void *p;

	p = malloc(n);
	if(p == nil)
		hfatal("malloc");
	memset(p, 0, n);
	return p;
}

void *
erealloc(void *ptr, ulong n)
{
	void *p;

	p = realloc(ptr, n);
	if(p == nil)
		hfatal("realloc");
	return p;
}

long
truestrlen(char *s)
{
	char *e;
	int waste;

	waste = 0;
	for(e = s; *e != 0; e++)
		if(*e == '%'){
			waste++;
			if(*(e+1) == '%'){
				e += 2;
				continue;
			}
			/* rudimentary but works for me. */
			while(isalnum(*++e) || *e == '.')
				waste++;
		}
	return e-s-waste;
}

int
numcmp(const void *a, const void *b)
{
	int na, nb;
	char **sa = (char **)a;
	char **sb = (char **)b;

	na = strtol(*sa, nil, 0);
	nb = strtol(*sb, nil, 0);
	return na - nb;
}

int
stringcmp(const void *a, const void *b)
{
	char **sa = (char **)a;
	char **sb = (char **)b;

	return strcmp(*sa, *sb);
}

static int
urldecode(char *url, char *out, long n)
{
	char *o, *ep;
	char xnum[3];
	int c;

	xnum[2] = 0;
	ep = url+n;
	for(o = out; url <= ep; o++){
		c = *url++;
		if(c == '%'){
			xnum[0] = url[0];
			xnum[1] = url[1];
			url += 2;
			if(!isxdigit(xnum[0]) || !isxdigit(xnum[1]))
				return -1;
			c = strtol(xnum, nil, 16);
		}
		*o = c;
	}
	return o - out;
}

int
mimetype(int fd, char *mime, long len)
{
	char m[256];
	uvlong n;
	int pf[2];
	char *argv[] = {
		"sh", "-c",
		"file -i - | sed 's/^.*:\\s*//' | tr -d '\\n'",
		nil,
	};

	memset(m, 0, sizeof m);
	if(pipe(pf) < 0)
		return -1;
	switch(fork()){
	case -1: return -1;
	case 0:
		close(pf[0]);
		dup2(fd, 0);
		dup2(pf[1], 1);
		close(pf[1]);
		execv("/bin/sh", argv);
		sysfatal("execl");
	default:
		close(pf[1]);
		if((n = read(pf[0], m, sizeof(m)-1)) < 0)
			return -1;
		close(pf[0]);
		/* file(1) is not that good at guessing. */
		if(strcmp(req->target, "/style") == 0)
			strncpy(m, "text/css; charset=utf-8", sizeof(m)-1);
		if(strncmp(m, "audio", 5) == 0)
			strncpy(m, "video", 5);
		strncpy(mime, m, len);
		wait(nil);
	}
	return 0;
}

int
filldirlist(char *path, char ***l, int *len)
{
	DIR *d;
	struct dirent *dir;

	d = opendir(path);
	if(d == nil)
		return -1;
	while((dir = readdir(d)) != nil)
		if(strcmp(dir->d_name, ".") != 0 &&
		   strcmp(dir->d_name, "..") != 0){
			*l = erealloc(*l, ++*len*sizeof(char *));
			(*l)[*len-1] = strdup(dir->d_name);
		}
	closedir(d);
	return 0;
}

void
insertepisode(Season *s, Episode *e, int no)
{
	Episode *ep, *olde;

	olde = nil;
	for(ep = s->pilot; ep != nil && ep->no < e->no; olde = ep, ep = ep->next)
		;
	if(olde == nil)
		s->pilot = e;
	else
		olde->next = e;
	e->next = ep;
}

HField *
allochdr(char *k, char *v)
{
	HField *h;

	h = emalloc(sizeof(HField));
	h->key = strdup(k);
	h->value = strdup(v);
	return h;
}

void
freehdr(HField *h)
{
	HField *hn;

	while(h != nil){
		hn = h->next;
		free(h->value);
		free(h->key);
		free(h);
		h = hn;
	}
}

void
inserthdr(HField **h, char *k, char *v)
{
	while(*h != nil)
		h = &(*h)->next;
	*h = allochdr(k, v);
}

char *
lookuphdr(HField *h, char *k)
{
	while(h != nil){
		if(strcmp(h->key, k) == 0)
			return h->value;
		h = h->next;
	}
	return nil;
}

Req *
allocreq(char *meth, char *targ, char *vers)
{
	Req *r;

	r = emalloc(sizeof(Req));
	r->method = strdup(meth);
	r->target = strdup(targ);
	r->version = strdup(vers);
	return r;
}

void
freereq(Req *r)
{
	freehdr(r->fields);
	free(r->version);
	free(r->target);
	free(r->method);
	free(r);
}

Res *
allocres(int sc)
{
	Res *r;

	r = emalloc(sizeof(Res));
	r->status = sc;
	inserthdr(&r->fields, "Server", srvname);
	return r;
}

void
freeres(Res *r)
{
	freehdr(r->fields);
	free(r);
}

int
hprint(char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = vprintf(fmt, ap);
	rc += printf("\r\n");
	va_end(ap);
	return rc;
}

void
hstline(int sc)
{
	hprint("%s %d %s", httpver, sc, statusmsg[sc]);
}

void
hprinthdr(void)
{
	HField *hp;

	hstline(res->status);
	for(hp = res->fields; hp != nil; hp = hp->next)
		hprint("%s: %s", hp->key, hp->value);
	hprint("");
	fflush(stdout);
}

void
hfail(int sc)
{
	char clen[16];

	res = allocres(sc);
	snprintf(clen, sizeof clen, "%u", strlen(errmsg));
	inserthdr(&res->fields, "Content-Type", "text/plain; charset=utf-8");
	inserthdr(&res->fields, "Content-Length", clen);
	hprinthdr();
	hprint("%s", errmsg);
	hprint("");
	exit(0);
}

void
hfatal(char *ctx)
{
	hstline(Sinternal);
	hprint("Content-Type: %s", "text/plain; charset=utf-8");
	hprint("Content-Length: %u", strlen(errmsg));
	hprint("");
	hprint("%s", errmsg);
	hprint("");
	fflush(stdout);
	sysfatal(ctx);
}

void
hparsereq(void)
{
	char *line, *meth, *targ, *vers, *k, *v;
	uint linelen;
	int n;

	n = getline(&line, &linelen, stdin);
	meth = strtok(line, " ");
	targ = strtok(nil, " ");
	vers = strtok(nil, " \r");
	if(meth == nil || targ == nil || vers == nil)
		hfail(Sbadreq);
	if(targ[strlen(targ)-1] == '/')
		targ[strlen(targ)-1] = 0;
	req = allocreq(meth, targ, vers);
	while((n = getline(&line, &linelen, stdin)) > 0){
		if(strcmp(line, "\r\n") == 0)
			break;
		k = strtok(line, ": ");
		v = strtok(nil, " \r");
		if(k == nil || v == nil)
			hfail(Sbadreq);
		inserthdr(&req->fields, k, v);
	}
}

void
sendfile(FILE *f, struct stat *fst)
{
	char buf[128*1024], mime[256], *s, crstr[6+3*16+1+1+1], clstr[16];
	uvlong brange[2], n, clen;

	n = clen = 0;
	if(mimetype(fileno(f), mime, sizeof mime) < 0)
		hfatal("sendfile: mimetype");
	clen = fst->st_size;
	if((s = lookuphdr(req->fields, "Range")) != nil){
		while(!isdigit(*++s) && *s != 0)
			;
		if(*s == 0)
			hfail(Sbadreq);
		brange[0] = strtoull(s, &s, 0);
		if(*s++ != '-')
			hfail(Sbadreq);
		if(!isdigit(*s))
			brange[1] = fst->st_size-1;
		else
			brange[1] = strtoull(s, &s, 0);
		if(brange[0] > brange[1] || brange[1] >= fst->st_size){
			res = allocres(Snotrange);
			snprintf(crstr, sizeof crstr, "bytes */%llu",
				fst->st_size);
		}else{
			res = allocres(Spartial);
			fseeko(f, brange[0], SEEK_SET);
			clen = brange[1]-brange[0]+1;
			snprintf(crstr, sizeof crstr, "bytes %llu-%llu/%llu",
				brange[0], brange[1], fst->st_size);
		}
		inserthdr(&res->fields, "Content-Range", crstr);
	}else
		res = allocres(Sok);
	inserthdr(&res->fields, "Accept-Ranges", "bytes");
	inserthdr(&res->fields, "Content-Type", mime);
	snprintf(clstr, sizeof clstr, "%llu", clen);
	inserthdr(&res->fields, "Content-Length", clstr);
	if((s = lookuphdr(req->fields, "Connection")) != nil)
		inserthdr(&res->fields, "Connection", s);
	hprinthdr();
	if(strcmp(req->method, "HEAD") == 0)
		return;
	while(clen -= n, !feof(f) && clen > 0){
		n = fread(buf, 1, sizeof buf, f);
		if(ferror(f))
			break;
		if(fwrite(buf, 1, n, stdout) <= 0)
			break;
	}
}

void
sendlist(char *path)
{
	FILE *f;
	struct stat fst;
	char **dirlist;
	int i, ndir;

	ndir = 0;
	dirlist = nil;
	f = tmpfile();
	if(f == nil)
		hfatal("sendlist: tmpfile");
	filldirlist(path, &dirlist, &ndir);
	qsort(dirlist, ndir, sizeof(char *), stringcmp);
	fprintf(f, listhead);
	fprintf(f, "<ul>\n");
	for(i = 0; i < ndir; i++)
		fprintf(f, "<li><a href=\"%s/%s\">%s</a></li>\n",
			strcmp(req->target, "/") == 0 ? "" : req->target,
			dirlist[i], dirlist[i]);
	fprintf(f, "</ul>\n");
	fprintf(f, listfeet);
	fseeko(f, 0, SEEK_SET);
	if(fstat(fileno(f), &fst) < 0)
		switch(errno){
		case EACCES: hfail(Sforbid);
		case ENOENT: hfail(Snotfound);
		default: hfatal("sendlist: fstat");
		}
	sendfile(f, &fst);
	fclose(f);
}

void
sendportal(char *path)
{
	Resource r;
	Part *p;
	Season *s;
	Episode *e;
	FILE *f, *auxf;
	struct stat fst;
	DIR *root, *d, *ed;
	struct dirent *rdir, *dir, *edir;
	char *title, *line, auxpath[512], buf[1024];
	uint linelen;
	int n, sno, canintrosubs, canintrodubs, canintroseason;

	p = nil;
	s = nil;
	e = nil;
	line = nil;
	sno = 0;
	memset(&r, 0, sizeof(Resource));
	r.type = Runknown;
	memset(auxpath, 0, sizeof auxpath);
	title = strrchr(path, '/');
	if(*++title == 0)
		hfail(Sbadreq);
	root = opendir(path);
	if(root == nil)
		hfatal("sendportal: opendir");
	fprintf(stdout, "%s\n", getcwd(nil, 0));
	while((rdir = readdir(root)) != nil){
		switch(r.type){
		case Runknown: break;
		case Rmovie:
			if(strcmp(rdir->d_name, "release") == 0){
				snprintf(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				f = fopen(auxpath, "r");
				if(f == nil)
					goto Rogue;
				n = getline(&line, &linelen, f);
				if(line[n-1] == '\n')
					line[(n--)-1] = 0;
				r.movie.release = strdup(line);
				fclose(f);
			}else if(strcmp(rdir->d_name, "synopsis") == 0)
				r.movie.hassynopsis++;
			else if(strcmp(rdir->d_name, "cover") == 0)
				r.movie.hascover++;
			else if(strcmp(rdir->d_name, "video") == 0)
				r.movie.hasvideo++;
			else if(strcmp(rdir->d_name, "history") == 0)
				r.movie.hashistory++;
			else if(strcmp(rdir->d_name, "sub") == 0){
				snprintf(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				filldirlist(auxpath, &r.movie.subs, &r.movie.nsub);
			}else if(strcmp(rdir->d_name, "dub") == 0){
				snprintf(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				filldirlist(auxpath, &r.movie.dubs, &r.movie.ndub);
			}else if(strcmp(rdir->d_name, "extra") == 0){
				snprintf(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				filldirlist(auxpath, &r.movie.extras, &r.movie.nextra);
			}else if(strcmp(rdir->d_name, "remake") == 0){
				snprintf(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				filldirlist(auxpath, &r.movie.remakes, &r.movie.nremake);
			}
			continue;
		case Rmulti:
			if(strcmp(rdir->d_name, "release") == 0){
				snprintf(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				f = fopen(auxpath, "r");
				if(f == nil)
					goto Rogue;
				n = getline(&line, &linelen, f);
				if(line[n-1] == '\n')
					line[(n--)-1] = 0;
				r.multi.release = strdup(line);
				fclose(f);
			}else if(strcmp(rdir->d_name, "synopsis") == 0)
				r.multi.hassynopsis++;
			else if(strcmp(rdir->d_name, "cover") == 0)
				r.multi.hascover++;
			else if(strcmp(rdir->d_name, "history") == 0)
				r.multi.hashistory++;
			else if(strncmp(rdir->d_name, "video", 5) == 0){
				if(p == nil){
					r.multi.part0 = emalloc(sizeof(Part));
					p = r.multi.part0;
				}else{
					p->next = emalloc(sizeof(Part));
					p = p->next;
				}
				p->no = strtol(rdir->d_name+5, nil, 0);
				snprintf(auxpath, sizeof auxpath, "%s/sub%d", path, p->no);
				filldirlist(auxpath, &p->subs, &p->nsub);
				snprintf(auxpath, sizeof auxpath, "%s/dub%d", path, p->no);
				filldirlist(auxpath, &p->dubs, &p->ndub);
			}else if(strcmp(rdir->d_name, "extra") == 0){
				snprintf(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				filldirlist(auxpath, &r.multi.extras, &r.multi.nextra);
			}else if(strcmp(rdir->d_name, "remake") == 0){
				snprintf(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				filldirlist(auxpath, &r.multi.remakes, &r.multi.nremake);
			}
			continue;
		case Rserie:
			if(strcmp(rdir->d_name, "synopsis") == 0)
				r.serie.hassynopsis++;
			else if(strcmp(rdir->d_name, "cover") == 0)
				r.serie.hascover++;
			else if(strcmp(rdir->d_name, "history") == 0)
				r.serie.hashistory++;
			else if(strcmp(rdir->d_name, "release") == 0){
				snprintf(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				f = fopen(auxpath, "r");
				if(f == nil)
					goto Rogue;
				while((n = getline(&line, &linelen, f)) > 0){
					sno++;
					if(line[n-1] == '\n')
						line[(n--)-1] = 0;
					if(!isdigit(*line))
						continue;
					if(s == nil){
						r.serie.s = emalloc(sizeof(Season));
						s = r.serie.s;
					}else{
						s->next = emalloc(sizeof(Season));
						s = s->next;
					}
					s->release = strdup(line);
					s->no = sno;
					e = nil;
					snprintf(auxpath, sizeof auxpath, "%s/s/%d", path, s->no);
					d = opendir(auxpath);
					if(d == nil)
						goto Rogue;
					while((dir = readdir(d)) != nil){
						if(!isdigit(dir->d_name[0]))
							continue;
						e = emalloc(sizeof(Episode));
						e->no = strtol(dir->d_name, nil, 0);
						insertepisode(s, e, e->no);
						/*
						 * it must be e->no instead of dir->d_name. we need to
						 * handle ranged episode folders, like `s/1/1-2' in
						 * Battlestar Galactica. or perhaps split the episode.
						 */
						snprintf(auxpath, sizeof auxpath, "%s/s/%d/%s", path, s->no, dir->d_name);
						ed = opendir(auxpath);
						if(ed == nil)
							goto Rogue;
						while((edir = readdir(ed)) != nil){
							if(strcmp(edir->d_name, "video") == 0)
								e->hasvideo++;
							else if(strcmp(edir->d_name, "sub") == 0){
								snprintf(auxpath, sizeof auxpath, "%s/s/%d/%d/%s", path, s->no, e->no, edir->d_name);
								filldirlist(auxpath, &e->subs, &e->nsub);
							}else if(strcmp(edir->d_name, "dub") == 0){
								snprintf(auxpath, sizeof auxpath, "%s/s/%d/%d/%s", path, s->no, e->no, edir->d_name);
								filldirlist(auxpath, &e->dubs, &e->ndub);
							}
						}
						closedir(ed);
					}
					closedir(d);
				}
				fclose(f);
			}else if(strcmp(rdir->d_name, "extra") == 0){
				snprintf(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				filldirlist(auxpath, &r.serie.extras, &r.serie.nextra);
			}else if(strcmp(rdir->d_name, "remake") == 0){
				snprintf(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				filldirlist(auxpath, &r.serie.remakes, &r.serie.nremake);
			}
			continue;
		}
		if(strcmp(rdir->d_name, "video") == 0)
			r.type = Rmovie;
		else if(strcmp(rdir->d_name, "video1") == 0)
			r.type = Rmulti;
		else if(strcmp(rdir->d_name, "s") == 0)
			r.type = Rserie;
		if(r.type != Runknown)
			rewinddir(root);
	}
	closedir(root);
	if(r.type == Runknown){
Rogue:
		sendlist(path);
		exit(0);
	}
	fprintf(stderr, "tmpfile incoming\n");
	f = tmpfile();
	if(f == nil)
		hfatal("sendportal: tmpfile");
	fprintf(f, portalhead, title, title);
	switch(r.type){
	case Rmovie:
		if(r.movie.hascover)
			fprintf(f, portalcover, req->target, req->target);
		fprintf(f, portalrelease);
		fwrite(r.movie.release, 1, strlen(r.movie.release), f);
		fprintf(f, portalmoviestream, req->target);
		if(r.movie.hassynopsis){
			fprintf(f, portalsynopsis);
			snprintf(auxpath, sizeof auxpath, "%s/synopsis", path);
			auxf = fopen(auxpath, "r");
			if(auxf == nil)
				break;
			while(!feof(auxf)){
				n = fread(buf, 1, sizeof buf, auxf);
				if(ferror(auxf))
					break;
				if(fwrite(buf, 1, n, f) <= 0)
					break;
			}
			fclose(auxf);
		}
		if(r.movie.hashistory){
			fprintf(f, portalhistory);
			snprintf(auxpath, sizeof auxpath, "%s/history", path);
			auxf = fopen(auxpath, "r");
			if(auxf == nil)
				break;
			while(!feof(auxf)){
				n = fread(buf, 1, sizeof buf, auxf);
				if(ferror(auxf))
					break;
				if(fwrite(buf, 1, n, f) <= 0)
					break;
			}
			fclose(auxf);
		}
		if(r.movie.nsub > 0){
			fprintf(f, portalsub);
			fprintf(f, "<ul>\n");
			for(; r.movie.nsub--; r.movie.subs++)
				fprintf(f, "<li><a href=\"%s/sub/%s\">%s</a></li>\n", req->target, *r.movie.subs, *r.movie.subs);
			fprintf(f, "</ul>");
		}
		if(r.movie.ndub > 0){
			fprintf(f, portaldub);
			fprintf(f, "<ul>\n");
			for(; r.movie.ndub--; r.movie.dubs++)
				fprintf(f, "<li><a href=\"%s/dub/%s\">%s</a></li>\n", req->target, *r.movie.dubs, *r.movie.dubs);
			fprintf(f, "</ul>");
		}
		if(r.movie.nextra > 0){
			fprintf(f, portalextra);
			fprintf(f, "<ul>\n");
			for(; r.movie.nextra--; r.movie.extras++)
				fprintf(f, "<li><a href=\"%s/extra/%s\">%s</a></li>\n", req->target, *r.movie.extras, *r.movie.extras);
			fprintf(f, "</ul>");
		}
		break;
	case Rmulti:
		if(r.multi.hascover)
			fprintf(f, portalcover, req->target, req->target);
		fprintf(f, portalrelease);
		fwrite(r.multi.release, 1, strlen(r.multi.release), f);
		fprintf(f, portalmultistream, req->target);
		fprintf(f, "<ul>\n");
		for(p = r.multi.part0; p != nil; p = p->next)
			fprintf(f, "<li><a href=\"%s/video%d\">Part %d</a></li>\n", req->target, p->no, p->no);
		fprintf(f, "</ul>");
		if(r.multi.hassynopsis){
			fprintf(f, portalsynopsis);
			snprintf(auxpath, sizeof auxpath, "%s/synopsis", path);
			auxf = fopen(auxpath, "r");
			if(auxf == nil)
				break;
			while(!feof(auxf)){
				n = fread(buf, 1, sizeof buf, auxf);
				if(ferror(auxf))
					break;
				if(fwrite(buf, 1, n, f) <= 0)
					break;
			}
			fclose(auxf);
		}
		if(r.multi.hashistory){
			fprintf(f, portalhistory);
			snprintf(auxpath, sizeof auxpath, "%s/history", path);
			auxf = fopen(auxpath, "r");
			if(auxf == nil)
				break;
			while(!feof(auxf)){
				n = fread(buf, 1, sizeof buf, auxf);
				if(ferror(auxf))
					break;
				if(fwrite(buf, 1, n, f) <= 0)
					break;
			}
			fclose(auxf);
		}
		canintrosubs = canintrodubs = 1;
		for(p = r.multi.part0; p != nil; p = p->next)
			if(p->nsub > 0){
				if(canintrosubs)
					fprintf(f, portalsub), canintrosubs--;
				fprintf(f, "<ul><li>Part %d", p->no);
				fprintf(f, "<ul>\n");
				for(; p->nsub--; p->subs++)
					fprintf(f, "<li><a href=\"%s/sub%d/%s\">%s</a></li>\n", req->target, p->no, *p->subs, *p->subs);
				fprintf(f, "</ul></li></ul>\n");
			}
		for(p = r.multi.part0; p != nil; p = p->next)
			if(p->ndub > 0){
				if(canintrodubs)
					fprintf(f, portaldub), canintrodubs--;
				fprintf(f, "<ul><li>Part %d", p->no);
				fprintf(f, "<ul>\n");
				for(; p->ndub--; p->dubs++)
					fprintf(f, "<li><a href=\"%s/dub%d/%s\">%s</a></li>\n", req->target, p->no, *p->dubs, *p->dubs);
				fprintf(f, "</ul></li></ul>\n");
			}
		if(r.movie.nextra > 0){
			fprintf(f, portalextra);
			fprintf(f, "<ul>\n");
			for(; r.movie.nextra--; r.movie.extras++)
				fprintf(f, "<li><a href=\"%s/extra/%s\">%s</a></li>\n", req->target, *r.movie.extras, *r.movie.extras);
			fprintf(f, "</ul>");
		}
		break;
	case Rserie:
		if(r.serie.hascover)
			fprintf(f, portalcover, req->target, req->target);
		fprintf(f, portalrelease);
		fprintf(f, "<ul>\n");
		for(s = r.serie.s; s != nil; s = s->next)
			fprintf(f, "<li>Season %d on %s</li>\n", s->no, s->release);
		fprintf(f, "</ul>");
		fprintf(f, portalseriestream);
		fprintf(f, "<ul>\n");
		for(s = r.serie.s; s != nil; s = s->next){
			fprintf(f, "<li>Season %d", s->no);
			fprintf(f, "<ul>\n");
			for(e = s->pilot; e != nil; e = e->next)
				if(e->hasvideo)
					fprintf(f, "<li><a href=\"%s/s/%d/%d/video\">Episode %d</a></li>\n",
						req->target, s->no, e->no, e->no);
				else
					fprintf(f, "<li>Episode %d is unavailable</li>\n", e->no);
			fprintf(f, "</ul></li>\n");
		}
		fprintf(f, "</ul>");
		if(r.serie.hassynopsis){
			fprintf(f, portalsynopsis);
			snprintf(auxpath, sizeof auxpath, "%s/synopsis", path);
			auxf = fopen(auxpath, "r");
			if(auxf == nil)
				break;
			while(!feof(auxf)){
				n = fread(buf, 1, sizeof buf, auxf);
				if(ferror(auxf))
					break;
				if(fwrite(buf, 1, n, f) <= 0)
					break;
			}
			fclose(auxf);
		}
		if(r.serie.hashistory){
			fprintf(f, portalhistory);
			snprintf(auxpath, sizeof auxpath, "%s/history", path);
			auxf = fopen(auxpath, "r");
			if(auxf == nil)
				break;
			while(!feof(auxf)){
				n = fread(buf, 1, sizeof buf, auxf);
				if(ferror(auxf))
					break;
				if(fwrite(buf, 1, n, f) <= 0)
					break;
			}
			fclose(auxf);
		}
		canintrosubs = canintrodubs = 1;
		for(s = r.serie.s; s != nil; s = s->next){
			canintroseason = 1;
			for(e = s->pilot; e != nil; e = e->next)
				if(e->nsub > 0){
					if(canintrosubs)
						fprintf(f, portalsub), canintrosubs--;
					if(canintroseason)
						fprintf(f, "<ul><li>Season %d", s->no), canintroseason--;
					fprintf(f, "<ul>\n");
					for(; e->nsub--; e->subs++)
						fprintf(f, "<li>Episode %d: <a href=\"%s/s/%d/%d/sub/%s\">%s</a></li>\n",
							e->no, req->target, s->no, e->no, *e->subs, *e->subs);
					fprintf(f, "</ul>");
				}
			if(!canintroseason)
				fprintf(f, "</li></ul>\n");
		}
		for(s = r.serie.s; s != nil; s = s->next){
			canintroseason = 1;
			for(e = s->pilot; e != nil; e = e->next)
				if(e->ndub > 0){
					if(canintrodubs)
						fprintf(f, portaldub), canintrodubs--;
					if(canintroseason)
						fprintf(f, "<ul><li>Season %d", s->no), canintroseason--;
					fprintf(f, "<ul>\n");
					for(; e->ndub--; e->dubs++)
						fprintf(f, "<li>Episode %d: <a href=\"%s/s/%d/%d/dub/%s\">%s</a></li>\n",
							e->no, req->target, s->no, e->no, *e->dubs, *e->dubs);
					fprintf(f, "</ul>");
				}
			if(!canintroseason)
				fprintf(f, "</li></ul>\n");
		}
		if(r.serie.nextra > 0){
			fprintf(f, portalextra);
			fprintf(f, "<ul>\n");
			for(; r.serie.nextra--; r.serie.extras++)
				fprintf(f, "<li><a href=\"%s/extra/%s\">%s</a></li>\n", req->target, *r.serie.extras, *r.serie.extras);
			fprintf(f, "</ul>");
		}
		break;
	default: goto Rogue;
	}
	fprintf(f, portalfeet);
	fseeko(f, 0, SEEK_SET);
	if(fstat(fileno(f), &fst) < 0)
		switch(errno){
		case EACCES: hfail(Sforbid);
		case ENOENT: hfail(Snotfound);
		default: hfatal("sendportal: fstat");
		}
	sendfile(f, &fst);
	fclose(f);
}

char *argv0;

void
usage(void)
{
	fprintf(stderr, "usage: %s [-d wdir] [-a assetsdir]\n", argv0);
	exit(1);
}

int
main(int argc, char *argv[])
{
	FILE *f;
	struct stat fst;
	char path[512];

	ARGBEGIN{
	case 'd':
		wdir = EARGF(usage());
		break;
	case 'a':
		assetpath = EARGF(usage());
		break;
	default: usage();
	}ARGEND;
	memset(path, 0, sizeof path);
	hparsereq();
	if(strcmp(req->method, "GET") != 0 && strcmp(req->method, "HEAD") != 0)
		hfail(Snotimple);
	/* "HTTP/1." */
	if(strncmp(req->version, httpver, 7) != 0)
		hfail(Swrongver);
	if(strcmp(req->target, "/style") == 0)
		snprintf(path, sizeof path, "%s/%s", assetpath, "style.css");
	else if(strcmp(req->target, "/favicon.ico") == 0)
		snprintf(path, sizeof path, "%s/%s", assetpath, "favicon.ico");
	else
		snprintf(path, sizeof path, "%s%s", wdir, req->target);
	if(urldecode(path, path, strlen(path)) < 0)
		hfail(Sbadreq);
	if(stat(path, &fst) < 0)
		switch(errno){
		case EACCES: hfail(Sforbid);
		case ENOENT: hfail(Snotfound);
		default: hfatal("stat");
		}
	if(S_ISREG(fst.st_mode)){
		f = fopen(path, "r");
		if(f == nil)
			hfatal("fopen");
		sendfile(f, &fst);
		fclose(f);
	}else
		sendportal(path);
	exit(0);
}
