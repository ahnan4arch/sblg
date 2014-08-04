/*	$Id$ */
/*
 * Copyright (c) 2013, 2014 Kristaps Dzonsons <kristaps@bsd.lv>,
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <assert.h>
#include <expat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "extern.h"

struct	linkall {
	FILE		*f; /* open template file */
	const char	*src; /* template file */
	XML_Parser	 p; /* active parser */
	struct article	*sargs; /* sorted article contents */
	int		 spos; /* current sarg being shown */ 
	int		 sposz; /* size of sargs */
	int		 ssposz;  /* size of sargs to show */
	size_t		 stack; /* temporary: tag stack size */
	size_t		 navlen; /* temporary: nav items to show */
	char		*navtag; /* temporary: only show tags */
	int		 navuse; /* use navigation contents */
	char		*nav; /* temporary: nav buffer */
	size_t		 navsz; /* nav buffer length */
};

static	void	tmpl_begin(void *dat, const XML_Char *s, 
			const XML_Char **atts);

static void
tmpl_text(void *dat, const XML_Char *s, int len)
{
	struct linkall	*arg = dat;

	fprintf(arg->f, "%.*s", len, s);
}

static void
tmpl_end(void *dat, const XML_Char *s)
{
	struct linkall	*arg = dat;

	if ( ! xmlvoid(s))
		fprintf(arg->f, "</%s>", s);
}

static void
article_begin(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct linkall	*arg = dat;

	arg->stack += 0 == strcasecmp(s, "article");
}

static void
nav_text(void *dat, const XML_Char *s, int len)
{
	struct linkall	*arg = dat;

	xmlappend(&arg->nav, &arg->navsz, s, len);
}

static void
nav_begin(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct linkall	*arg = dat;

	arg->stack += 0 == strcasecmp(s, "nav");
	xmlrappendopen(&arg->nav, &arg->navsz, s, atts);
}

/*
 * Find a given tag "needle" in the space-separated set of tags
 * "haystack".
 * If "needle" is NULL or the tag was found, return 1.
 * If "haystack" is NULL or the tag wasn't found, return 0.
 */
static int
tfind(const char *needle, const char *haystack)
{
	const char 	*cp;
	size_t	 	 sz;

	if (NULL == needle)
		return(1);
	if (NULL == haystack) 
		return(0);

	sz = strlen(needle);
again:
	if (NULL == (cp = strstr(haystack, needle)))
		return(0);

	if (cp > haystack && ' ' != cp[-1]) {
		haystack = cp + sz;
		goto again;
	}

	cp += sz;
	if ('\0' == *cp || ' ' == *cp)
		return(1);

	haystack = cp;
	goto again;
}

static void
nav_end(void *dat, const XML_Char *s)
{
	struct linkall	*arg = dat;
	size_t		 i, j, start;
	char		 buf[32]; /* large enough for date */
	int		 k, inprint;

	if (strcasecmp(s, "nav") || 0 != --arg->stack) {
		xmlrappendclose(&arg->nav, &arg->navsz, s);
		return;
	}

	XML_SetElementHandler(arg->p, tmpl_begin, tmpl_end);
	XML_SetDefaultHandlerExpand(arg->p, tmpl_text);

	fprintf(arg->f, "\n<ul>\n");

	/*
	 * If we haven't been provided a navigation template (i.e., what
	 * was within the navigation tags), then make a simple default
	 * consisting of a list entry.
	 */
	if ( ! arg->navuse || 0 == arg->navsz) {
		i = 0;
		for (k = 0; i < arg->navlen && k < arg->sposz; k++) {
			/* Are we limiting by tag? */
			if ( ! tfind(arg->navtag, arg->sargs[k].tags))
				continue;
			(void)strftime(buf, sizeof(buf), 
				"%Y-%m-%d", 
				localtime(&arg->sargs[k].time));
			fprintf(arg->f, "<li>\n");
			fprintf(arg->f, "%s: ", buf);
			fprintf(arg->f, 
				"<a href=\"%s\">%s</a>\n",
				arg->sargs[k].src,
				arg->sargs[k].titletext);
			fprintf(arg->f, "</li>\n");
			i++;
		}
		fprintf(arg->f, "</ul>\n");
		fprintf(arg->f, "</%s>", s);
		free(arg->nav);
		free(arg->navtag);
		arg->navsz = 0;
		arg->nav = arg->navtag = NULL;
		return;
	}

	/*
	 * We do have a navigation template.
	 * Output it, replacing key terms along the way.
	 */
#define	STRCMP(_word, _sz) (j - start == (_sz) && \
	0 == memcmp(&arg->nav[start], (_word), (_sz)))

	for (i = 0, k = 0; i < arg->navlen && k < arg->sposz; k++) {
		/* Are we limiting by tag? */
		if ( ! tfind(arg->navtag, arg->sargs[k].tags))
			continue;
		strftime(buf, sizeof(buf), "%Y-%m-%d", 
			localtime(&arg->sargs[k].time));
		inprint = 0;
		fprintf(arg->f, "<li>\n");
		for (j = 1; j < arg->navsz; j++) {
			if ('$' != arg->nav[j - 1]) {
				fputc(arg->nav[j - 1], arg->f);
				continue;
			} else if ('{' != arg->nav[j]) {
				fputc(arg->nav[j - 1], arg->f);
				continue;
			}
			start = ++j;
			inprint = 1;
			for ( ; j < arg->navsz; j++) 
				if ('}' == arg->nav[j])
					break;
			if (j == arg->navsz)
				break;
			if (STRCMP("base", 4))
				fputs(arg->sargs[k].base, arg->f);
			else if (STRCMP("title", 5))
				fputs(arg->sargs[k].title, arg->f);
			else if (STRCMP("titletext", 9))
				fputs(arg->sargs[k].titletext, arg->f);
			else if (STRCMP("author", 6))
				fputs(arg->sargs[k].author, arg->f);
			else if (STRCMP("authortext", 10))
				fputs(arg->sargs[k].authortext, arg->f);
			else if (STRCMP("source", 6))
				fputs(arg->sargs[k].src, arg->f);
			else if (STRCMP("date", 4))
				fputs(buf, arg->f);
			else if (STRCMP("aside", 5) &&
				NULL != arg->sargs[k].aside)
				fputs(arg->sargs[k].aside, arg->f);

			if (j < arg->navsz)
				j++;
			inprint = 0;
		}
		if ( ! inprint)
			fputc(arg->nav[j - 1], arg->f);
		fprintf(arg->f, "</li>\n");
		i++;
	}
	fprintf(arg->f, "</ul>\n");
	fprintf(arg->f, "</%s>", s);
	free(arg->nav);
	free(arg->navtag);
	arg->navsz = 0;
	arg->nav = arg->navtag = NULL;
}

static void
empty_end(void *dat, const XML_Char *s)
{
	struct linkall	*arg = dat;

	if (0 == strcasecmp(s, "article") && 0 == --arg->stack) {
		XML_SetElementHandler(arg->p, tmpl_begin, tmpl_end);
		XML_SetDefaultHandlerExpand(arg->p, tmpl_text);
	}
}

static void
article_end(void *dat, const XML_Char *s)
{
	struct linkall	*arg = dat;

	if (0 == strcasecmp(s, "article") && 0 == --arg->stack) {
		XML_SetElementHandler(arg->p, tmpl_begin, tmpl_end);
		XML_SetDefaultHandlerExpand(arg->p, tmpl_text);
	}
}

static int
scmp(const void *p1, const void *p2)
{
	const struct article *s1 = p1, *s2 = p2;

	return(difftime(s2->time, s1->time));
}

static void
tmpl_begin(void *dat, const XML_Char *s, const XML_Char **atts)
{
	struct linkall	 *arg = dat;
	const XML_Char	**attp;

	assert(0 == arg->stack);

	if (0 == strcasecmp(s, "nav")) {
		/*
		 * Only handle if containing the "data-sblg-nav"
		 * attribute, otherwise continue.
		 */
		xmlprint(arg->f, s, atts);
		for (attp = atts; NULL != *attp; attp += 2) 
			if (0 == strcasecmp(*attp, "data-sblg-nav"))
				break;

		if (NULL == *attp || ! xmlbool(attp[1]))
			return;

		/*
		 * Take the number of elements to show to be the min of
		 * the full count or as user-specified.
		 */
		arg->navuse = 0;
		arg->navlen = arg->sposz;
		for (attp = atts; NULL != *attp; attp += 2) {
			if (0 == strcasecmp(attp[0], 
					"data-sblg-navsz")) {
				arg->navlen = atoi(attp[1]);
				if (arg->navlen > (size_t)arg->sposz)
					arg->navlen = arg->sposz;
			} else if (0 == strcasecmp(attp[0], 
					"data-sblg-navcontent")) {
				arg->navuse = xmlbool(attp[1]);
			} else if (0 == strcasecmp(attp[0], 
					"data-sblg-navtag")) {
				free(arg->navtag);
				arg->navtag = strdup(attp[1]);
			}
		}

		arg->stack++;
		XML_SetElementHandler(arg->p, nav_begin, nav_end);
		XML_SetDefaultHandlerExpand(arg->p, nav_text);
		return;
	} else if (strcasecmp(s, "article")) {
		xmlprint(arg->f, s, atts);
		return;
	}

	/*
	 * Only consider article elements if they contain the magic
	 * data-sblg-article attribute.
	 */
	for (attp = atts; NULL != *attp; attp += 2) 
		if (0 == strcasecmp(*attp, "data-sblg-article"))
			break;

	if (NULL == *attp || ! xmlbool(attp[1])) {
		xmlprint(arg->f, s, atts);
		return;
	}

	if (arg->spos > arg->ssposz) {
		/*
		 * We have no articles left to show.
		 * Just continue throwing away this article element til
		 * we receive a matching one.
		 */
		arg->stack++;
		XML_SetDefaultHandlerExpand(arg->p, NULL);
		XML_SetElementHandler(arg->p, article_begin, empty_end);
		return;
	}

	/*
	 * First throw away children, then push out the article itself.
	 */
	arg->stack++;
	XML_SetDefaultHandlerExpand(arg->p, NULL);
	XML_SetElementHandler(arg->p, article_begin, article_end);
	if ( ! echo(arg->f, 1, arg->sargs[arg->spos++].src))
		XML_StopParser(arg->p, 0);
	for (attp = atts; NULL != *attp; attp += 2) 
		if (0 == strcasecmp(*attp, "data-sblg-permlink"))
			break;
	if (NULL != *attp && ! xmlbool(attp[1]))
		return;
	fprintf(arg->f, "<div data-sblg-permlink=\"1\"><a href=\"%s\">"
			"permanent link</a></div>", 
			arg->sargs[arg->spos - 1].src);
}


/*
 * Given a set of articles "src", grok articles from the files, then
 * fill in a template that's usually the blog "front page".
 */
int
linkall(XML_Parser p, const char *templ, const char *force,
		int sz, char *src[], const char *dst)
{
	char		*buf;
	size_t		 ssz;
	int		 i, fd, rc;
	FILE		*f;
	struct linkall	 larg;
	struct article	*sarg;

	ssz = 0;
	rc = 0;
	buf = NULL;
	fd = -1;
	f = NULL;

	memset(&larg, 0, sizeof(struct linkall));
	sarg = xcalloc(sz, sizeof(struct article));

	/*
	 * Grok all article data.
	 * These can either be the origin XML files or those formatted
	 * with the compile() function into HTML.
	 */
	for (i = 0; i < sz; i++)
		if ( ! grok(p, src[i], &sarg[i]))
			goto out;

	/* Sort by date. */
	qsort(sarg, sz, sizeof(struct article), scmp);

	f = stdout;
	if (strcmp(dst, "-") && (NULL == (f = fopen(dst, "w")))) {
		perror(dst);
		goto out;
	} 
	
	if ( ! mmap_open(templ, &fd, &buf, &ssz))
		goto out;

	larg.sargs = sarg;
	larg.sposz = larg.ssposz = sz;
	larg.p = p;
	larg.src = templ;
	larg.f = f;

	/*
	 * If we're going to force a single entry to be shown, then find
	 * it in our arguments.
	 */
	if (NULL != force) {
		for (i = 0; i < sz; i++)
			if (0 == strcmp(force, sarg[i].src))
				break;

		if (i < sz) {
			larg.spos = i;
			larg.ssposz = i + 1;
		}
	}

	/*
	 * Run the XML parser on the template.
	 */
	XML_ParserReset(p, NULL);
	XML_SetDefaultHandlerExpand(p, tmpl_text);
	XML_SetElementHandler(p, tmpl_begin, tmpl_end);
	XML_SetUserData(p, &larg);

	if (XML_STATUS_OK != XML_Parse(p, buf, (int)ssz, 1)) {
		fprintf(stderr, "%s:%zu:%zu: %s\n", templ, 
			XML_GetCurrentLineNumber(p),
			XML_GetCurrentColumnNumber(p),
			XML_ErrorString(XML_GetErrorCode(p)));
		goto out;
	} 

	fputc('\n', f);
	rc = 1;
out:
	for (i = 0; i < sz; i++) 
		grok_free(&sarg[i]);
	mmap_close(fd, buf, ssz);
	if (NULL != f && stdout != f)
		fclose(f);

	free(larg.nav);
	free(larg.navtag);
	free(sarg);
	return(rc);
}

