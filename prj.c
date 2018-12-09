#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#if defined(__TOS__) || defined(__atarist__)
#ifdef __GNUC__
#include <osbind.h>
#define DOSTIME _DOSTIME
#else
#include <tos.h>
#endif
#endif
#include "pcmake.h"
#  ifndef CLK_TCK
#   define CLK_TCK	CLOCKS_PER_SEC
#  endif


typedef struct
{
	unsigned long bytes;
	unsigned long lines;
	unsigned long cbytes;
	unsigned long clines;
	unsigned long start;
	unsigned long end;
	unsigned long time;
} STATS;

static STATS stats;


/**************************************************************************/
/* ---------------------------------------------------------------------- */
/**************************************************************************/

/* remove trailing whitespace */
static void chomp(char *ln)
{
	if (*ln)
	{
		char *t = ln + strlen(ln);

		t--;
		while (t >= ln && (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n'))
		{
			*t = 0;
			t--;
		}
	}
}

/* ---------------------------------------------------------------------- */

static char nextchar(char **ln)					/* skip only analysed character */
{
	if (**ln)
		return *(++(*ln));
	return 0;
}

/* ---------------------------------------------------------------------- */

/* skip white space */
static char skipwhite(char **ln)
{
	char c;

	while ((c = **ln) == ' ' || c == '\t' || c == '\r' || c == '\n')
		++(*ln);
	return c;							/* can be zero: end input */
}

/* ---------------------------------------------------------------------- */

/* skip analysed character and following white space */
static char skipchar(char **ln)
{
	if (**ln)
		++(*ln);
	return skipwhite(ln);
}

/* ---------------------------------------------------------------------- */

static bool peekstr(const char *ln, const char *s, int l)
{
	while (l)
	{
		if (*s != *ln)
			return false;
		if (*s == 0 || *ln == 0)
			return false;
		--l, ++s, ++ln;
	}
	return true;
}

/* ---------------------------------------------------------------------- */

/* terminated by space or ( or ) or , */
static char *parse_filename(char **ln)
{
	char c;
	char *s, *p, *str;
	size_t len;
	
	s = *ln;
	p = s;
	for (;;)
	{
		c = *p;
		if (c == 0 || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '(' || c == ')' || c == ',')
			break;
		p++;
	}
	*ln = p;
	len = p - s;
	str = malloc(len + 1);
	if (str != NULL)
	{
		memcpy(str, s, len);
		str[len] = '\0';
	}
	return str;
}

/**************************************************************************/
/* ---------------------------------------------------------------------- */
/**************************************************************************/

static bool inlist(filearg *list, const char *f)
{
	while (list)
	{
		if (stricmp(list->name, f) == 0)
			return true;
		list = list->next;
	}
	return false;
}


static filearg *putflist(filearg **list, const char *f, FTY type)
{
	filearg *entry = NULL;
	
	if (!inlist(*list, f))
	{
		entry = malloc(sizeof(*entry) + strlen(f));
		if (entry)
		{
			strcpy(entry->name, f);
			entry->next = NULL;
			entry->filetype = type;
			entry->u.cflags = NULL;
			entry->dependencies = NULL;
			while (*list)
				list = &(*list)->next;
			*list = entry;
		}
	}
	return entry;
}


/*
 *  keepfile(prj, f, l) - remember the filename 'f' in the appropriate place
 */
static filearg *keepfile(PRJ *prj, const char *f)
{
	FTY ftype;
	filearg *entry = NULL;
	
	if (f == NULL)
		return NULL;

	ftype = filetype(f);
	switch (ftype)
	{
	case FT_NONE:
		/* no suffix */
		/* default is .C */
		ftype = FT_CSOURCE;
		{
			char *ps;
	
			ps = malloc(strlen(f) + 3);
			if (ps)
			{
				strcpy(ps, f);
				strcat(ps, ".c");
				entry = putflist(&prj->inputs, ps, ftype);
				free(ps);
			}
		}
		break;
	case FT_PROGRAM:
	case FT_SHAREDLIB:
		entry = putflist(&prj->inputs, f, ftype);
		break;
	case FT_LIBRARY:
		entry = putflist(&prj->inputs, f, ftype);
		break;
	case FT_CSOURCE:
	case FT_ASSOURCE:
		entry = putflist(&prj->inputs, f, ftype);
		if (entry)
			entry->u.cflags = copy_cflags(&prj->c_flags);
		break;
	case FT_OBJECT:
		entry = putflist(&prj->inputs, f, ftype);
		break;
	case FT_HEADER:
		entry = putflist(&prj->inputs, f, ftype);
		break;
	case FT_PROJECT:
		entry = putflist(&prj->inputs, f, ftype);
		break;
	default:
		break;
	}
	return entry;
}


static void default_output_file(PRJ *prj)
{
	filearg *ft;

	/*
	 * FIXME maybe: PureC uses project filename as default
	 */
	if (prj->output == NULL)
	{
		for (ft = prj->inputs; ft != NULL; ft = ft->next)
		{
			if (ft->filetype == FT_CSOURCE || ft->filetype == FT_ASSOURCE)
				break;
		}
		if (ft && (ft->filetype == FT_CSOURCE || ft->filetype == FT_ASSOURCE))
		{
			prj->output = change_suffix(ft->name, suff_prg);
		} else
		{
			prj->output = strdup("a.prg");
		}
		prj->output_type = filetype(prj->output);
	}
}


static void clear_project(PRJ *prj, int level)
{
	filearg *ft, *next;

	if (prj)
	{
		for (ft = prj->inputs; ft != NULL; ft = next)
		{
			next = ft->next;
			
			switch (ft->filetype)
			{
			case FT_CSOURCE:
			case FT_ASSOURCE:
				free_cflags(ft->u.cflags);
				free(ft->u.cflags);
				break;
			case FT_PROJECT:
				clear_project(ft->u.prj, level + 1);
				break;
			default:
				break;
			}
			list_free(&ft->dependencies);
			free(ft);
		}
		free(prj->output);
		free(prj->filename);
		free(prj->directory);
		
		free_cflags(&prj->c_flags);
		free_ldflags(&prj->ld_flags);
		
		free(prj);
	}
}


void free_project(PRJ *prj)
{
	clear_project(prj, 0);
}


static bool older(const DOSTIME *t1, const DOSTIME *t2)
{
	if (t1->date < t2->date)
		return true;
	if (t1->date > t2->date)
		return false;
	if (t1->time < t2->time)
		return true;
	return false;
}


static char *objname_for_src(PRJ *prj, filearg *ft)
{
	char *objname;
	char *tmp;
	
	if (prj->c_flags.output_directory)
	{
		tmp = build_path(prj->c_flags.output_directory, basename(ft->name));
		objname = change_suffix(tmp, suff_o);
		free(tmp);
	} else
	{
		tmp = build_path(prj->directory, ft->name);
		objname = change_suffix(tmp, suff_o);
		free(tmp);
	}
	return objname;
}




static void touch(PRJ *prj, filearg *ft)
{
	int h;
	DOSTIME t;
	char *name;
	
	t.time = 0;
	t.date = 0x21;
	name = build_path(prj->directory, ft->name);
	if ((h = (int)Fopen(name, FO_READ)) >= 0)
	{
		char *o;

		Fclose(h);
		o = objname_for_src(prj, ft);
		if ((h = (int)Fopen(o, FO_RW)) >= 0)
		{
			Fdatime(&t, h, 1);			/* touch .o (datime --> 80-1-1) */
			Fclose(h);
		}
		free(o);
	}
	free(name);
}


static int makeok(PRJ *prj, filearg *ft, const char *objname, int lvl)
{
	int fh;
	DOSTIME obj_timestamp, src_timestamp;
	strlist *dep;
	char *f;
	
	fh = (int)Fopen(objname, FO_READ);
	if (fh <= 0)
		return 1;						/* .o absent, must compile */

	Fdatime(&obj_timestamp, fh, 0);				/* object (target) */
	Fclose(fh);
	fh = (int)Fopen(ft->name, FO_READ);
	if (fh <= 0)
	{
		errout(_("%d>%s: Can't open %s\n"), lvl, program_name, ft->name);
		return 0;
	}

	Fdatime(&src_timestamp, fh, 0);
	Fclose(fh);
	
	if (older(&src_timestamp, &obj_timestamp))
	{
		/* src older than object; check dependencies */
		for (dep = ft->dependencies; dep; dep = dep->next)
		{
			f = build_path(prj->directory, dep->str);
			fh = (int)Fopen(f, FO_READ);
			if (fh <= 0)
			{
				errout(_("%d>%s: Can't open %s\n"), lvl, program_name, f);
				free(f);
				return -1;
			}
			free(f);
			Fdatime(&src_timestamp, fh, 0);
			Fclose(fh);
			if (older(&obj_timestamp, &src_timestamp))
				return 1;
		}
		return 0;
	}
	return 1;		/* we must compile */
}



static void add_arg(int *argc, char ***argv, const char *arg)
{
	*argv = realloc(*argv, (*argc + 2) * sizeof(char *));
	if (*argv == NULL)
		return;
	(*argv)[*argc] = strdup(arg);
	++(*argc);
	(*argv)[*argc] = NULL;
}


/* called by docomp() */
static void prj_params(const C_FLAGS *cflags, int *argc, char ***argv)
{
	const strlist *str;
	
	for (str = cflags->defines; str != NULL; str = str->next)
	{
		add_arg(argc, argv, "-D");
		add_arg(argc, argv, str->str);
	}

	for (str = cflags->undefines; str != NULL; str = str->next)
	{
		add_arg(argc, argv, "-U");
		add_arg(argc, argv, str->str);
	}

	for (str = cflags->includes; str != NULL; str = str->next)
	{
		add_arg(argc, argv, "-I");
		add_arg(argc, argv, str->str);
	}
}



/*
 * docomp(prj, f) - run the compiler on the given .c file
 */
static int docomp(PRJ *prj, filearg *ft)
{
	int warn;
	int argc;
	char **argv;
	char buf[32];
	const C_FLAGS *cflags = ft->u.cflags;
	bool need_ahcc;
	bool is_asm = ft->filetype == FT_ASSOURCE;
	char *srcname;
	char *output_name;
	
	argc = 0;
	argv = NULL;
	add_arg(&argc, &argv, is_asm ? "as" : "cc");

	/* many levels of verbosity */
	if (cflags->verbose > 0)
		add_arg(&argc, &argv, "-V");
	if (cflags->verbose > 1)
		add_arg(&argc, &argv, "-V");
	if (cflags->verbose > 2)
		add_arg(&argc, &argv, "-V");
	if (cflags->verbose > 3)
		add_arg(&argc, &argv, "-V");
	if (cflags->output_DRI)
		add_arg(&argc, &argv, "-B");
	if (is_asm)
	{
		if (cflags->supervisor)
			add_arg(&argc, &argv, "-S");						/* default .super in assembly */
		if (cflags->undefined_external)
			add_arg(&argc, &argv, "-U");
	} else
	{
		if (cflags->nested_comments)
			add_arg(&argc, &argv, "-C");	/* nested comments */
		if (cflags->max_errors >= 0)
		{
			add_arg(&argc, &argv, "-E");
			sprintf(buf, "%d", cflags->max_errors);
			add_arg(&argc, &argv, buf);
		}
		if (cflags->max_warnings >= 0)
		{
			add_arg(&argc, &argv, "-F");
			sprintf(buf, "%d", cflags->max_warnings);
			add_arg(&argc, &argv, buf);
		}
		if (cflags->cdecl_calling)
			add_arg(&argc, &argv, "-H");					/* cdecl calling */
		if (cflags->no_jump_optimization)
			add_arg(&argc, &argv, "-J");
		if (cflags->char_is_unsigned)
			add_arg(&argc, &argv, "-K");					/* default char is unsigned */
		if (cflags->identifier_max_length >= 0)
		{
			add_arg(&argc, &argv, "-L");
			sprintf(buf, "%d", cflags->identifier_max_length);
			add_arg(&argc, &argv, buf);
		}
		if (cflags->string_merging)
			add_arg(&argc, &argv, "-M");
		if (cflags->absolute_calls)
			add_arg(&argc, &argv, "-P");
		if (cflags->pascal_calling)
			add_arg(&argc, &argv, "-Q");
		if (cflags->no_register_vars)
			add_arg(&argc, &argv, "-R");	/* suppress registerization */
		if (cflags->frame_pointer)
			add_arg(&argc, &argv, "-S");
		if (cflags->stack_checking)
			add_arg(&argc, &argv, "-T");
		if (cflags->add_underline)
			add_arg(&argc, &argv, "-X");
		if (cflags->no_register_reload)
			add_arg(&argc, &argv, "-Z");	/* suppress registerization */
		if (cflags->warning_enabled[WARN_GOT])
			add_arg(&argc, &argv, "-W+got");	/* warn goto's */
		if (cflags->default_int32)
			add_arg(&argc, &argv, "--mno-short");					/* default int is 32 bits */
	}
	if (cflags->i2_68020)
		add_arg(&argc, &argv, "-2");	/* >= 68020 */
	if (cflags->i2_68030)
		add_arg(&argc, &argv, "-3");	/* 68030 */
	if (cflags->i2_68040)
		add_arg(&argc, &argv, "-4");	/* 68040 */
	if (cflags->i2_68851)
		add_arg(&argc, &argv, "-5");	/* 68851 */
	if (cflags->i2_68060)
		add_arg(&argc, &argv, "-6");	/* 68060 */
	if (cflags->use_FPU)
		add_arg(&argc, &argv, "-8");	/* FPU */
	if (cflags->Coldfire)
		add_arg(&argc, &argv, "-7");	/* double is 64 bits */
	if (cflags->debug_infos)
		add_arg(&argc, &argv, "-Y");
	if (cflags->no_output)
		add_arg(&argc, &argv, "--fno-output");

	srcname = build_path(prj->directory, ft->name);
	
#if 0
	if (cflags->output_directory != NULL && cflags->output_directory[0] != '\0')
	{
		add_arg(&argc, &argv, "-N");
		add_arg(&argc, &argv, cflags->output_directory);
	}
#endif

	prj_params(cflags, &argc, &argv);

	if (cflags->output_name)
	{
		output_name = build_path(prj->directory, cflags->output_name);
	} else
	{
		output_name = objname_for_src(prj, ft);
	}
	
	add_arg(&argc, &argv, "-O");
	add_arg(&argc, &argv, output_name);

	add_arg(&argc, &argv, srcname);
	
	if (verbose > 0)
	{
		int i;

		fprintf(stdout, _("\n****  Compiling %s\n"), srcname);
		fprintf(stdout, "%s", argv[0]);
		for (i = 1; i < argc; i++)
			fprintf(stdout, " %s", argv[i]);
		fprintf(stdout, "\n");
	}
	free(output_name);
	free(srcname);
	
	need_ahcc = cflags->Coldfire || cflags->default_int32;
	
	if ((warn = compiler(need_ahcc, argc, (const char **)argv)) > 0)
	{
#if 0
		if (o)
		{
			Fdelete(o);
		} else
		{
			P_path pn;

			pn.s = f;
			S_path sf;

			sf = change_suffix(pn.t, sufs.s);
			Fdelete(sf.s);
		}
#endif
	}

	strfreev(argv);
	
	if (warn == 0)
	{
		if (verbose > 0)
			fprintf(stdout, _("compilation OK\n"));
	}

	return warn;
}


static char *look_CC(PRJ *prj, filearg *ft, const char *msg)
{
	char *name;
	bool found;
	
	name = build_path(prj->directory, ft->name);
	if (file_exists(name) || is_absolute_path(ft->name))
	{
		found = true;
	} else if ((ft->filetype == FT_OBJECT && ft == prj->inputs) || ft->filetype == FT_LIBRARY)
	{
		name = build_path(get_libdir(), ft->name);
		found = file_exists(name);
	} else
	{
		found = false;
	}
	if (!found)
	{
		errout(_("can't find %s '%s'\n"), msg, ft->name);
		free(name);
		name = NULL;
	}

	return name;
}


/*
 * dold() - run the loader
 */

static bool dold(PRJ *prj)
{
	char **argv;
	int argc;
	int rep = 0;
	char buf[50];
	char *name;
	
	if (prj->inputs == NULL)
	{
		errout(_("nothing to link\n"));
		return false;
	}
	
	default_output_file(prj);

	argc = 0;
	argv = malloc(sizeof(char *));
	add_arg(&argc, &argv, "ld");
	if (prj->ld_flags.verbose > 0)
		add_arg(&argc, &argv, "-V");
	if (prj->ld_flags.verbose > 1)
		add_arg(&argc, &argv, "-V");
	if (prj->ld_flags.verbose > 2)
		add_arg(&argc, &argv, "-V");
	if (prj->ld_flags.verbose > 3)
		add_arg(&argc, &argv, "-V");
	if (prj->ld_flags.global_symbols)
		add_arg(&argc, &argv, "-G");
	if (prj->ld_flags.local_symbols)
		add_arg(&argc, &argv, "-L");
	if (prj->ld_flags.nm_list)
		add_arg(&argc, &argv, "-N");
	if (prj->ld_flags.load_map)
		add_arg(&argc, &argv, "-P");
	if (prj->ld_flags.debug_infos)
		add_arg(&argc, &argv, "-Y");
	if (prj->ld_flags.create_new_object)
		add_arg(&argc, &argv, "-J");
	if (prj->ld_flags.malloc_for_stram)
		add_arg(&argc, &argv, "-M");
	if (prj->ld_flags.no_fastload)
		add_arg(&argc, &argv, "-F");
	if (prj->ld_flags.program_to_stram)
		add_arg(&argc, &argv, "-R");
	if (prj->ld_flags.heap_size > 0)
	{
		sprintf(buf, "-H=0x%08lx", prj->ld_flags.heap_size);
		add_arg(&argc, &argv, buf);
	}
	if (prj->ld_flags.stacksize > 0)
	{
		sprintf(buf, "-S=0x%08lx", prj->ld_flags.stacksize);
		add_arg(&argc, &argv, buf);
	}
	if (prj->ld_flags.text_start > 0)
	{
		sprintf(buf, "-T=0x%08lx", prj->ld_flags.text_start);
		add_arg(&argc, &argv, buf);
	}
	if (prj->ld_flags.data_start > 0)
	{
		sprintf(buf, "-D=0x%08lx", prj->ld_flags.data_start);
		add_arg(&argc, &argv, buf);
	}
	if (prj->ld_flags.bss_start > 0)
	{
		sprintf(buf, "-B=0x%08lx", prj->ld_flags.bss_start);
		add_arg(&argc, &argv, buf);
	}
	if (prj->ld_flags.nm_list)
		add_arg(&argc, &argv, "--symbol-list");
	if (prj->ld_flags.load_map)
		add_arg(&argc, &argv, "--map");
	add_arg(&argc, &argv, "-O");
	add_arg(&argc, &argv, prj->output);
		
	if (prj->inputs)
	{
		filearg *ft = prj->inputs;
		
		while (rep == 0 && ft)
		{
			switch (ft->filetype)
			{
			case FT_CSOURCE:
				name = objname_for_src(prj, ft);
				add_arg(&argc, &argv, name);
				free(name);
				break;
			case FT_ASSOURCE:
				name = objname_for_src(prj, ft);
				add_arg(&argc, &argv, name);
				free(name);
				break;
			case FT_OBJECT:
				if ((name = look_CC(prj, ft, _("start up"))) == NULL)
				{
					rep = 1;
				} else
				{
					add_arg(&argc, &argv, name);
				}
				free(name);
				break;
			case FT_LIBRARY:
				if ((name = look_CC(prj, ft, _("library"))) == NULL)
				{
					rep = 1;
				} else
				{
					add_arg(&argc, &argv, name);
				}
				free(name);
				break;
			default:
				break;
			}
			ft = ft->next;
		}
	}
	argv[argc] = NULL;
	
	if (rep == 0)
	{
		if (verbose > 0)
		{
			int i;
			
			fprintf(stdout, _("\n****  Linking %s\n"), prj->filename);
			fprintf(stdout, "%s", argv[0]);
			for (i = 1; i < argc; i++)
				fprintf(stdout, " %s", argv[i]);
			fprintf(stdout, "\n");
		}
		
		rep = linker(argc, (const char **)argv);
		if (rep)
		{
			errout(_("linker failed: %d\n"), rep);
			if (prj->output_type != FT_PROJECT)
				Fdelete(prj->output);
		}
	}
	
	strfreev(argv);
	
	return rep == 0;
}



static int make_prj(PRJ *prj, bool ignore_date, int level)
{
	int r, anycomp = 0;
	int fh;
	filearg *ft;
	char *name;
		
	for (ft = prj->inputs; ft; ft = ft->next)
	{
		if (ft->filetype == FT_CSOURCE || ft->filetype == FT_ASSOURCE)
		{
			char *of = objname_for_src(prj, ft);

			if (ignore_date)
				r = 1;
			else
				r = makeok(prj, ft, of, level);	/* recursive check timestamp */
			free(of);
			if (r < 0)
				return r;
			
			anycomp += r;

			if (r == 1)
			{
				if (docomp(prj, ft) > 0)
					return -1;		/* errors */
			}
		} else if (ft->filetype == FT_PROJECT)
		{
			r = make_prj(ft->u.prj, ignore_date, level + 1);
	
			if (r < 0)
				return -r;					/* errors */
			anycomp += r;
		}

		ft = ft->next;
	}

	if (anycomp == 0)
	{
		if (ignore_date)
		{
			anycomp = 1;
		} else
		{
			DOSTIME prg_timestamp, src_timestamp;
			
			if (prj->output == NULL || (fh = (int)Fopen(prj->output, 0)) < 0)
			{
				anycomp = 1;
			} else
			{
				Fdatime(&prg_timestamp, fh, 0);
				Fclose(fh);
				for (ft = prj->inputs; ft; ft = ft->next)
				{
					switch (ft->filetype)
					{
					case FT_CSOURCE:
						name = objname_for_src(prj, ft);
						break;
					case FT_ASSOURCE:
						name = objname_for_src(prj, ft);
						break;
					case FT_OBJECT:
						if ((name = look_CC(prj, ft, _("start up"))) == NULL)
						{
							return -1;
						}
						break;
					case FT_LIBRARY:
						if ((name = look_CC(prj, ft, _("library"))) == NULL)
						{
							return -1;
						}
						break;
					default:
						name = build_path(prj->directory, ft->name);
						break;
					}
					fh = (int)Fopen(name, FO_READ);
					if (fh <= 0)
					{
						errout(_("%d>%s: Can't open %s\n"), level, program_name, name);
						free(name);
						return -1;
					}
					free(name);
					Fdatime(&src_timestamp, fh, 0);
					Fclose(fh);
					if (older(&prg_timestamp, &src_timestamp))
					{
						anycomp = 1;
						break;
					}
				}
			}
		}
	}
	
	if (anycomp > 0)
	{
		if (dold(prj) == false)						/* run the loader */
			anycomp = -1;
	}
	
	return anycomp;
}


bool domake(PRJ *prj, bool ignore_date)
{
	int anycomp;

	if (prj->inputs == NULL)
	{
		errout(_("nothing to make\n"));
	} else
	{
		anycomp = make_prj(prj, ignore_date, 0);	/* recursive make */

		if (anycomp < 0)
			return false;
		
		if (anycomp == 0)
			fprintf(stdout, _("%s: %s is up to date\n"), program_name, prj->output ? prj->output : prj->filename);
	}
	return true;
}


static void clear_dates(PRJ *prj, int level)
{
	filearg *ft;

	for (ft = prj->inputs; ft; ft = ft->next)
	{
		switch (ft->filetype)
		{
		case FT_CSOURCE:
		case FT_ASSOURCE:
			touch(prj, ft);
			break;
		case FT_PROJECT:
			clear_dates(ft->u.prj, level + 1);
			break;
		default:
			break;
		}
	}
}


bool domakeall(PRJ *prj)				/* updates all objects filestamp, call make */
{
	unsigned long cmins, csecs, secs;

	memset(&stats, 0, sizeof(stats));
	stats.start = clock();
	
	/*
	 * we will ignore timestamps on this run,
	 * but it might still be useful to clear the dates
	 * in case the shell is restarted
	 */
	clear_dates(prj, 0);

	if (!domake(prj, true))					/* run make */
		return false;

	stats.end = clock();
	stats.time = stats.end - stats.start;
	csecs = stats.time / CLK_TCK;
	cmins = csecs / 60;
	secs = csecs % 60;
	fprintf(stdout, _("\nMake_all statistics\n"));
	fprintf(stdout, _("Project:\n"));
	fprintf(stdout, _("bytes  : %7ld\n"), stats.bytes);
	fprintf(stdout, _("lines  : %7ld\n"), stats.lines);
	if (stats.lines)
		fprintf(stdout, ("       = %7ld bytes/line\n"), stats.bytes / stats.lines);
	fprintf(stdout, _("Compiled:\n"));
	fprintf(stdout, _("bytes  : %7ld\n"), stats.cbytes);
	fprintf(stdout, _("lines  : %7ld\n"), stats.clines);
	if (stats.clines)
		fprintf(stdout, _("       = %7ld bytes/line\n"), stats.cbytes / stats.clines);
	fprintf(stdout, _("time   : %4ld'%02ld\" (%ld)\n"), cmins, secs, csecs);
	if (csecs)
	{
		fprintf(stdout, _("       = %7ld bytes/second\n"), stats.cbytes / csecs);
		fprintf(stdout, _("         %7ld lines/second\n"), stats.clines / csecs);
	}
	return true;
}


static char *add_options(char **fro)
{
	char *p, *s;
	char *to;
	
	if (skipwhite(fro) == '[')
		skipchar(fro);

	s = *fro;
	p = s;
	while (*p && *p != '\r' && *p != '\n' && *p != ']')
		p++;

	to = strndup(s, p - s);
	if (*p == ']')
		*p++ = 0;
	*fro = p;
	return to;
}


static PRJ *load_prj(const char *f, int level)
{
	FILE *fp;
	char st[1024];
	char *s;
	bool firstfile = true;
	int section = 0;
	long lineno;
	bool retval = true;
	PRJ *prj;
	filearg *keep;
	
	if ((fp = fopen(f, "r")) == NULL)
	{
		errout(_("%d>Can't open project file: %s\n"), level, f);
		return NULL;
	}

	prj = (PRJ *)malloc(sizeof(*prj));
	if (prj == NULL)
	{
		fclose(fp);
		return NULL;
	}
	init_cflags(&prj->c_flags);
	init_ldflags(&prj->ld_flags);

	prj->filename = strdup(f);
	prj->directory = dirname(f);
	prj->inputs = NULL;
	prj->output = NULL;
	prj->output_type = FT_NONE;
	
	lineno = 0;
	while (fgets(st, (int)sizeof(st) - 1, fp) != 0)
	{
		char c;

		lineno++;

		s = st;
		while (*s && *s != ';')
			s++;						/* remove comment */
		if (*s == ';')
			*s = 0;
		
		s = st;
		chomp(s);

		if (*s)							/* anything left ? */
		{
			if (verbose >= 2)
				fprintf(stdout, "%d>'%s'\n", level, s);

			c = skipwhite(&s);
			if (c == '.' && !peekstr(s, "..", 2))
			{
				bool result;
				
				c = nextchar(&s);
				if (c == 'C' || c == 'c')
				{
					char *lopt;
					
					skipchar(&s);
					lopt = add_options(&s);
					if (lopt == NULL)
						result = false;
					else
						result = parse_cc_options(lopt, &prj->c_flags);
					free(lopt);
				} else if (c == 'S' || c == 's')
				{
					char *lopt;
					
					skipchar(&s);
					lopt = add_options(&s);
					if (lopt == NULL)
						result = false;
					else
						result = parse_as_options(lopt, &prj->c_flags);
					free(lopt);
				} else if (c == 'L' || c == 'l')
				{
					char *lopt;
					
					skipchar(&s);
					lopt = add_options(&s);
					if (lopt == NULL)
						result = false;
					else
						result = parse_ld_options(lopt, &prj->ld_flags);
					free(lopt);
				} else
				{
					result = false;
				}
				if (result == false)
				{
					errout(_("%s: %s:%ld: Illegal option specification\n"), Error, f, lineno);
					retval = false;
				}
			} else if (c == '=')
			{
				if (section != 0)
				{
					errout(_("%s: %s:%ld: Illegal filename\n"), Error, f, lineno);
					retval = false;
				} else
				{
					/* TODO what if no output file in project?
					   i think PureC uses name of project file in this case */
					section++;
				}
				firstfile = true;
			} else if (c != 0 && c != '=')
			{
				char *ps, *fnm;

				skipwhite(&s);
				fnm = parse_filename(&s);
				ps = build_path(prj->directory, fnm);
				free(fnm);
				
				if (firstfile)
				{
					firstfile = false;
					if (section == 0)
					{
						if (prj->output)
						{
							errout(_("%s: duplicate output file '%s'\n"), program_name, ps);
							free(ps);
							return FT_UNKNOWN;
						}
						/* TODO: replace "*.PRG" in filename with editor filename, for DEFAULT.PRJ */
						prj->output = ps;
						prj->output_type = filetype(prj->output);
						if (prj->output_type == FT_NONE)
							prj->output_type = FT_PROGRAM;
						continue;
					}
				}
				
				keep = keepfile(prj, ps);
				if (keep == NULL || keep->filetype == FT_UNKNOWN)
				{
					errout(_("%d>%s: %s:%ld: unknown file suffix '%s'\n"), level, Error, f, lineno, ps);
					retval = false;
					free(ps);
				} else
				{
					free(ps);
					if (keep->filetype != FT_NONE)
					{
						if (skipwhite(&s) == '(')	/* has dependencies */
						{
							/* only for prj_dependencies */
							for (;;)
							{
								c = skipchar(&s);	/* either ( or , */
								fnm = parse_filename(&s);
								if (fnm && fnm[0])
								{
									ps = build_path(prj->directory, fnm);
									list_append(&keep->dependencies, ps);
									free(ps);
								}
								free(fnm);
								if (skipwhite(&s) != ',')
									break;
							}
							if (skipwhite(&s) != ')')
							{
								errout(_("%s: %s:%ld: ')' expected\n"), Error, f, lineno);
								retval = false;
							} else
							{
								skipchar(&s);
							}
						}
						
						if (skipwhite(&s) == '[')
						{
							char *opt;
							bool result;
							
							opt = add_options(&s);
							if (opt == NULL)
								result = false;
							else if (keep->filetype == FT_CSOURCE)
								result = parse_cc_options(opt, keep->u.cflags);
							else if (keep->filetype == FT_ASSOURCE)
								result = parse_as_options(opt, keep->u.cflags);
							else
								result = false;
							if (result == false)
							{
								errout(_("%s: %s:%ld: Illegal option specification: %s\n"), Error, f, lineno, opt);
								retval = false;
							}
							free(opt);
						}
						
						/* nested project */
						if (keep->filetype == FT_PROJECT)
						{
							char *name = build_path(prj->directory, keep->name);
							keep->u.prj = load_prj(name, level + 1);
							free(name);
							if (keep->u.prj == NULL)
								retval = false;
						}
					}
				}
			}
		}
	}

	fclose(fp);

	if (retval)
	{
		default_output_file(prj);
		if (prj->output_type == FT_LIBRARY)
			prj->ld_flags.create_new_object = true;
	} else
	{
		free_project(prj);
		prj = NULL;
	}
	
	return prj;
}


PRJ *loadmake(const char *f)
{
	if (verbose >= 1)
		fprintf(stdout, _("Loading project file %s\n"), f);
	
	return load_prj(f, 0);
}