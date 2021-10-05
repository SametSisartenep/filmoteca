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
#include <pwd.h>
#include <signal.h>
#include <pthread.h>
#include <utf.h>
#include <fmt.h>
#include "dat.h"
#include "fns.h"
#include "args.h"

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

char httpver[] = "HTTP/1.1";
char srvname[] = "filmoteca";
char errmsg[] = "NO MOVIES HERE";
char listhead[] = "<!doctype html>\n<html>\n<head>\n"
	"<meta charset=\"utf-8\">\n"
	"<link rel=\"stylesheet\" href=\"/style\" media=\"all\" type=\"text/css\"/>\n"
	"<title>Filmoteca</title>\n"
	"</head>\n<body>\n"
	"<h1>Filmoteca</h1>\n";
char listfeet[] = "</body>\n</html>\n";
char portalhead[] = "<!doctype html>\n<html>\n<head>\n"
	"<meta charset=\"utf-8\">\n"
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
char *wdir = "/filmoteca";
int lfd, ncpu;
pthread_t *threads;
pthread_mutex_t attendlock;
Index catalog;

int debug;
char *argv0;


uint
hash(char *s)
{
	uint h;

	h = 0x811c9dc5;
	while(*s != 0)
		h = (h*0x1000193) ^ (uchar)*s++;
	return h % INDEXSIZE;
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
	free(h->value);
	free(h->key);
	free(h);
}

void
inserthdr(Req *r, char *k, char *v)
{
	HField *h;

	if(r->headers == nil){
		r->headers = allochdr(k, v);
		return;
	}
	for(h = r->headers; h->next != nil; h = h->next)
		;
	h->next = allochdr(k, v);
}

char *
lookuphdr(Req *r, char *k)
{
	HField *h;

	for(h = r->headers; h != nil; h = h->next)
		if(strcmp(h->key, k) == 0)
			return h->value;
	return nil;
}

Req *
allocreq(char *meth, char *targ, char *vers, int sc)
{
	Req *r;

	r = emalloc(sizeof(Req));
	r->method = meth != nil? strdup(meth): nil;
	r->target = targ != nil? strdup(targ): nil;
	r->version = vers != nil? strdup(vers): strdup(httpver);
	r->status = sc;
	r->headers = nil;
	inserthdr(r, "Server", srvname);
	return r;
}

void
freereq(Req *r)
{
	HField *h, *hn;

	for(h = r->headers; h != nil; h = hn){
		hn = h->next;
		freehdr(h);
	}
	free(r->version);
	free(r->target);
	free(r->method);
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

int
hprintst(Req *r)
{
	return hprint("%s %d %s", r->version, r->status, statusmsg[r->status]);
}

int
hprinthdr(Req *r)
{
	HField *h;
	int rc;

	rc = hprintst(r);
	for(h = r->headers; h != nil; h = h->next)
		rc += hprint("%s: %s", h->key, h->value);
	rc += hprint("");
	fflush(stdout);
	return rc;
}

void
hfail(int sc)
{
	Req *res;
	char clen[16];

	res = allocreq(nil, nil, nil, sc);
	snprint(clen, sizeof clen, "%u", strlen(errmsg));
	inserthdr(res, "Content-Type", "text/plain; charset=utf-8");
	inserthdr(res, "Content-Length", clen);
	hprinthdr(res);
	freereq(res);
	hprint("%s", errmsg);
	hprint("");
	exit(1);
}

Req *
hparsereq(void)
{
	Req *req;
	char *line, *linep, *meth, *targ, *vers, *k, *v;
	ulong linelen;
	int n;

	line = nil;
	linelen = 0;
	n = getline(&line, &linelen, stdin);
	meth = strtok_r(line, " ", &linep);
	targ = strtok_r(nil, " ", &linep);
	vers = strtok_r(nil, " \r", &linep);
	if(meth == nil || targ == nil || vers == nil)
		hfail(Sbadreq);

	if(targ[strlen(targ)-1] == '/')
		targ[strlen(targ)-1] = 0;

	req = allocreq(meth, targ, vers, 0);

	while((n = getline(&line, &linelen, stdin)) > 0){
		if(strcmp(line, "\r\n") == 0)
			break;
		k = strtok_r(line, ": ", &linep);
		v = strtok_r(nil, " \r", &linep);
		if(k == nil || v == nil)
			hfail(Sbadreq);
		inserthdr(req, k, v);
	}
	free(line);

	return req;
}

Movie *
allocmovie()
{

}

void
freemovie(Movie *m)
{

}

Multipart *
allocmultipart()
{

}

void
freemultipart(Multipart *m)
{

}

Series *
allocserie()
{

}

void
freeserie(Series *s)
{

}

Resource *
allocresource(char *title, int type, void *p)
{
	Resource *r;

	r = emalloc(sizeof(Resource));
	r->title = strdup(title);
	r->type = type;
	r->media = p;
	r->next = nil;
}

void
freeresource(Resource *r)
{
	free(r->title);
	switch(r->type){
	case Rmovie:
		freemovie(r->media);
		break;
	case Rmulti:
		freemultipart(r->media);
		break;
	case Rserie:
		freeserie(r->media);
		break;
	}
	free(r);
}

void
addresource(Index *idx, Resource *r)
{
	Resource *rp;
	uint h;

	h = hash(r->title);
	r->next = idx->rtab[h];
	idx->rtab[h] = r;
}

void
delresource(Index *idx, char *title)
{
	Resource *rp, *rn;
	uint h;

	h = hash(title);
	if(strcmp(idx->rtab[h]->title, title) == 0){
		rn = idx->rtab[h]->next;
		freeresource(idx->rtab[h]);
		idx->rtab[h] = rn;
		return;
	}
	for(rp = idx->rtab[h]; rp->next != nil; rp = rp->next)
		if(strcmp(rp->next->title, title) == 0){
			rn = rp->next->next;
			freeresource(rp->next);
			rp->next = rn;
			break;
		}
}

void
initindex(Index *idx)
{
	Resource *r;
	Movie *m;
	Multipart *mp;
	Series *s;
	void *media;
	DIR *dp;
	dirent *dpe;
	char **dirs, *d, path[512];
	int ndirs, rtype;

	dirs = nil;
	ndirs = 0;

	filldirlist(wdir, &dirs, &ndirs);
	while(ndirs > 0){
		rtype = Runknown;
		d = dirs[--ndirs];

		snprint(path, sizeof path, "%s/%s", wdir, d);
		dp = opendir(path);

		while((dpe = readdir(dp)) != nil)
			if(strcmp(dpe->d_name, "video") == 0){
				rtype = Rmovie;
				break;
			}else if(strcmp(dpe->d_name, "video1") == 0){
				rtype = Rmulti;
				break;
			}else if(strcmp(dpe->d_name, "s") == 0){
				rtype = Rserie;
				break;
			}

		if(rtype != Runknown)
			rewinddir(dp);
		else{
			closedir(dp);
			free(d);
			continue;
		}

		switch(rtype){
		case Rmovie:
			m = allocmovie();
			media = m;
			break;
		case Rmulti:
			mp = allocmultipart();
			media = mp;
			break;
		case Rserie:
			s = allocserie();
			media = s;
			break;
		}

		r = allocresource(d, rtype, media);
		addresource(idx, r);

		free(d);
	}
}

void
sendfile(Req *req, FILE *f, struct stat *fst)
{
	Req *res;
	char buf[128*1024], mime[256], *s, crstr[6+3*16+1+1+1], clstr[16];
	uvlong brange[2], n, clen;

	n = clen = 0;
	if(strcmp(req->target, "/style") == 0)
		strncpy(mime, "text/css; charset=utf-8", sizeof mime);
	else if(mimetype(fileno(f), mime, sizeof mime) < 0)
		hfail(Sinternal);
	clen = fst->st_size;
	if((s = lookuphdr(req, "Range")) != nil){
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
			res = allocreq(nil, nil, nil, Snotrange);
			snprint(crstr, sizeof crstr, "bytes */%llu",
				fst->st_size);
		}else{
			res = allocreq(nil, nil, nil, Spartial);
			fseeko(f, brange[0], SEEK_SET);
			clen = brange[1]-brange[0]+1;
			snprint(crstr, sizeof crstr, "bytes %llu-%llu/%llu",
				brange[0], brange[1], fst->st_size);
		}
		inserthdr(res, "Content-Range", crstr);
	}else
		res = allocreq(nil, nil, nil, Sok);
	inserthdr(res, "Accept-Ranges", "bytes");
	inserthdr(res, "Content-Type", mime);
	snprint(clstr, sizeof clstr, "%llu", clen);
	inserthdr(res, "Content-Length", clstr);
	if((s = lookuphdr(req, "Connection")) != nil)
		inserthdr(res, "Connection", s);
	hprinthdr(res);
	freereq(res);
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
sendlist(Req *req, char *path)
{
	FILE *f;
	struct stat fst;
	char **dirlist;
	int i, ndir;

	ndir = 0;
	dirlist = nil;
	f = tmpfile();
	if(f == nil)
		hfail(Sinternal);
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
		default: hfail(Sinternal);
		}
	sendfile(req, f, &fst);
	fclose(f);
}

void
sendportal(Req *req, char *path)
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
	ulong linelen;
	int n, sno, canintrosubs, canintrodubs, canintroseason;

	p = nil;
	s = nil;
	e = nil;
	line = nil;
	linelen = 0;
	sno = 0;

	memset(&r, 0, sizeof(Resource));
	r.type = Runknown;

	memset(auxpath, 0, sizeof auxpath);

	title = strrchr(path, '/');
	if(*++title == 0)
		hfail(Sbadreq);

	root = opendir(path);
	if(root == nil)
		hfail(Sinternal);

	while((rdir = readdir(root)) != nil){
		switch(r.type){
		case Runknown: break;
		case Rmovie:
			if(strcmp(rdir->d_name, "release") == 0){
				snprint(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				f = fopen(auxpath, "r");
				if(f == nil)
					goto Rogue;
				n = getline(&line, &linelen, f);
				if(line[n-1] == '\n')
					line[(n--)-1] = 0;
				r.movie.release = strdup(line);
				free(line);
				line = nil;
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
				snprint(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				filldirlist(auxpath, &r.movie.subs, &r.movie.nsub);
			}else if(strcmp(rdir->d_name, "dub") == 0){
				snprint(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				filldirlist(auxpath, &r.movie.dubs, &r.movie.ndub);
			}else if(strcmp(rdir->d_name, "extra") == 0){
				snprint(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				filldirlist(auxpath, &r.movie.extras, &r.movie.nextra);
			}else if(strcmp(rdir->d_name, "remake") == 0){
				snprint(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				filldirlist(auxpath, &r.movie.remakes, &r.movie.nremake);
			}
			continue;
		case Rmulti:
			if(strcmp(rdir->d_name, "release") == 0){
				snprint(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				f = fopen(auxpath, "r");
				if(f == nil)
					goto Rogue;
				n = getline(&line, &linelen, f);
				if(line[n-1] == '\n')
					line[(n--)-1] = 0;
				r.multi.release = strdup(line);
				free(line);
				line = nil;
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
				snprint(auxpath, sizeof auxpath, "%s/sub%d", path, p->no);
				filldirlist(auxpath, &p->subs, &p->nsub);
				snprint(auxpath, sizeof auxpath, "%s/dub%d", path, p->no);
				filldirlist(auxpath, &p->dubs, &p->ndub);
			}else if(strcmp(rdir->d_name, "extra") == 0){
				snprint(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				filldirlist(auxpath, &r.multi.extras, &r.multi.nextra);
			}else if(strcmp(rdir->d_name, "remake") == 0){
				snprint(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
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
				snprint(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
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
					free(line);
					line = nil;
					s->no = sno;
					e = nil;
					snprint(auxpath, sizeof auxpath, "%s/s/%d", path, s->no);
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
						snprint(auxpath, sizeof auxpath, "%s/s/%d/%s", path, s->no, dir->d_name);
						ed = opendir(auxpath);
						if(ed == nil)
							goto Rogue;
						while((edir = readdir(ed)) != nil){
							if(strcmp(edir->d_name, "video") == 0)
								e->hasvideo++;
							else if(strcmp(edir->d_name, "sub") == 0){
								snprint(auxpath, sizeof auxpath, "%s/s/%d/%d/%s", path, s->no, e->no, edir->d_name);
								filldirlist(auxpath, &e->subs, &e->nsub);
							}else if(strcmp(edir->d_name, "dub") == 0){
								snprint(auxpath, sizeof auxpath, "%s/s/%d/%d/%s", path, s->no, e->no, edir->d_name);
								filldirlist(auxpath, &e->dubs, &e->ndub);
							}
						}
						closedir(ed);
					}
					closedir(d);
				}
				fclose(f);
			}else if(strcmp(rdir->d_name, "extra") == 0){
				snprint(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
				filldirlist(auxpath, &r.serie.extras, &r.serie.nextra);
			}else if(strcmp(rdir->d_name, "remake") == 0){
				snprint(auxpath, sizeof auxpath, "%s/%s", path, rdir->d_name);
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
		sendlist(req, path);
		exit(0);
	}
	f = tmpfile();
	if(f == nil)
		hfail(Sinternal);
	if(debug)
		fprint(2, "tmpfile set up\n");
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
			snprint(auxpath, sizeof auxpath, "%s/synopsis", path);
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
			snprint(auxpath, sizeof auxpath, "%s/history", path);
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
			snprint(auxpath, sizeof auxpath, "%s/synopsis", path);
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
			snprint(auxpath, sizeof auxpath, "%s/history", path);
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
			snprint(auxpath, sizeof auxpath, "%s/synopsis", path);
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
			snprint(auxpath, sizeof auxpath, "%s/history", path);
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
		default: hfail(Sinternal);
		}
	sendfile(req, f, &fst);
	fclose(f);
}

void
srvfilms(void)
{
	Req *req;
	FILE *f;
	struct stat fst;
	char path[512];

	memset(path, 0, sizeof path);

	req = hparsereq();
	if(debug)
		fprint(2, "received:\n\tmethod: %s\n\ttarget: %s\n\tversion: %s\n",
			req->method, req->target, req->version);

	if(strcmp(req->method, "GET") != 0 && strcmp(req->method, "HEAD") != 0)
		hfail(Snotimple);
	/* "HTTP/1." */
	if(strncmp(req->version, httpver, 7) != 0)
		hfail(Swrongver);

	if(strcmp(req->target, "/style") == 0)
		snprint(path, sizeof path, "%s/%s", assetpath, "style.css");
	else if(strcmp(req->target, "/favicon.ico") == 0)
		snprint(path, sizeof path, "%s/%s", assetpath, "favicon.ico");
	else
		snprint(path, sizeof path, "%s%s", wdir, req->target);

	if(urldecode(path, path, strlen(path)) < 0)
		hfail(Sbadreq);
	if(debug)
		fprint(2, "localtarget: %s\n", path);

	if(stat(path, &fst) < 0)
		switch(errno){
		case EACCES: hfail(Sforbid);
		case ENOENT: hfail(Snotfound);
		default: hfail(Sinternal);
		}

	if(S_ISREG(fst.st_mode)){
		f = fopen(path, "r");
		if(f == nil)
			hfail(Sinternal);
		sendfile(req, f, &fst);
		fclose(f);
	}else
		sendportal(req, path);
	freereq(req);
}

void *
tmain(void *a)
{
	char caddr[128];
	int cfd;

	for(;;){
		pthread_mutex_lock(&attendlock);
		if((cfd = acceptcall(lfd, caddr, sizeof caddr)) < 0)
			sysfatal("acceptcall: %r");
		pthread_mutex_unlock(&attendlock);
		if(debug)
			fprint(2, "thr#%lu accepted call from %s\n", pthread_self(), caddr);

		switch(fork()){
		case 0:
			dup2(cfd, 0);
			dup2(cfd, 1);
			close(cfd);
			srvfilms();
			exit(0);
		default:
			signal(SIGCHLD, SIG_IGN);
			//wait(nil);
			/* FALLTHROUGH */
		case -1:
			break;
		}

		close(cfd);
		if(debug)
			fprint(2, "thr#%lu ended call with %s\n", pthread_self(), caddr);
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-D] [-d wdir] [-a assetsdir] [-p port] [-u user]\n", argv0);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int i, lport;
	char *runusr;
	struct passwd *pwd;

	lport = 8080;
	runusr = nil;
	ARGBEGIN{
	case 'a':
		assetpath = EARGF(usage());
		break;
	case 'd':
		wdir = EARGF(usage());
		break;
	case 'p':
		lport = strtol(EARGF(usage()), nil, 10);
		break;
	case 'u':
		runusr = EARGF(usage());
		break;
	case 'D':
		debug++;
		break;
	default: usage();
	}ARGEND;
	if(argc != 0)
		usage();

	if((lfd = listentcp(lport)) < 0)
		sysfatal("listen: %r");

	if(runusr != nil){
		pwd = getpwnam(runusr);
		if(pwd == nil)
			sysfatal("getpwnam: %r");
		/* good practice */
		if(setgroups(0, nil) < 0)
			sysfatal("setgroups: %r");
		if(setgid(pwd->pw_gid) < 0)
			sysfatal("setgid: %r");
		if(setuid(pwd->pw_uid) < 0)
			sysfatal("setuid: %r");
	}

	ncpu = sysconf(_SC_NPROCESSORS_ONLN);
	if(ncpu < 1)
		ncpu = 1;

	threads = emalloc(sizeof(pthread_t)*ncpu);
	pthread_mutex_init(&attendlock, nil);

	for(i = 0; i < ncpu; i++){
		pthread_create(threads+i, nil, tmain, nil);
		if(debug)
			fprint(2, "created thr#%lu\n", *(threads+i));
	}

	pause();

	free(threads);
	pthread_mutex_destroy(&attendlock);

	exit(0);
}
