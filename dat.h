#define nil NULL

typedef unsigned char uchar;
typedef long long vlong;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned long long uvlong;
typedef uint32_t u32int;
typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;
typedef struct dirent dirent;

typedef struct Movie Movie;
typedef struct Multipart Multipart;
typedef struct Part Part;
typedef struct Series Series;
typedef struct Season Season;
typedef struct Episode Episode;
typedef struct Resource Resource;
typedef struct Index Index;

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
	char *title;
	int type;
	void *media;
	/* this union will disappear soon */
	union {
		Movie movie;
		Multipart multi;
		Series serie;
	};
	Resource *next;
};

enum {
	INDEXSIZE	= 67,
};
struct Index {
	pthread_rwlock_t lock;
	Resource *rtab[INDEXSIZE];
};

typedef struct Req Req;
typedef struct HField HField;

struct Req {
	char *method, *target, *version;
	int status;
	HField *headers;
};

struct HField {
	char *key, *value;
	HField *next;
};
