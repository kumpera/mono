#include <mono/metadata/profiler-private.h>
#include <mini.h>
#include <stdio.h>

/*
Usage:

Run your program like this:
mono --record-compilation=comp.txt basic.exe

Then pass prof=comp.txt to the aot compiler:
mono --aot=prof=comp.txt basic.exe

Notes:
comp.txt has info on all assemblies, so it can be used when AOT'ing all assemblies
*/
typedef struct {
	FILE *file;
} RecorderProfiler;

RecorderProfiler prof;

static void
runtime_shutdown (MonoProfiler *_)
{
	fflush (prof.file);
	fclose (prof.file);
	prof.file = NULL;
}

static void
jit_end (MonoProfiler *_, MonoMethod *method, MonoJitInfo *jinfo, int result)
{
	if (result != MONO_PROFILE_OK)
		return;
	char *name = mono_method_full_name  (method, TRUE);
	fprintf (prof.file, "%s ## %s\n", method->klass->image->assembly_name, name);
	g_free (name);
}

void
aot_profiler_init_recording (char *profiling_file)
{
	prof.file = fopen (profiling_file, "w");
	mono_profiler_install ((MonoProfiler*)&prof, runtime_shutdown);
	mono_profiler_install_jit_end (jit_end);
	mono_profiler_set_events (MONO_PROFILE_JIT_COMPILATION);
}

static GHashTable *filtering_table;
void
aot_profiler_init_filtering (char *profiling_file)
{
	FILE *f = fopen (profiling_file, "r");
	if (!f)
		return;

	filtering_table = g_hash_table_new (g_str_hash, g_str_equal);
	char *line = NULL;
	size_t capacity = 0;
	ssize_t len;
	while ((len = getline (&line, &capacity, f)) > 0) {
		char *module = line;
		char *method = strchr (line, '#');
		if (!method)
			continue;
		method [-1] = '\0'; //end the previous string
		method += 3; //skip second # and first space.
		line [len - 1] = 0; //kill the line break
		
		// printf ("line: %s\n\tmodule %s\n\tmethod '%s'\n", line, module, method);

		//find module
		GHashTable *module_table = g_hash_table_lookup (filtering_table, module);
		if (!module_table) {
			module_table = g_hash_table_new (g_str_hash, g_str_equal);
			g_hash_table_insert (filtering_table, g_strdup (module), module_table);
		}

		if (!g_hash_table_lookup (module_table, method))
			g_hash_table_insert (module_table, g_strdup (method), GUINT_TO_POINTER (1));
	}
	// exit (0);
}

gboolean
aot_profiler_filter_method (MonoMethod *method)
{
	if (!filtering_table)
		return TRUE;

	MonoImage *img = method->klass->image;
	GHashTable *module_table = g_hash_table_lookup (filtering_table, img->assembly_name);
	char *method_name = mono_method_full_name (method, TRUE);
	if (!module_table) {
		// printf ("skipping %s we don't care about image %s\n", method_name, img->assembly_name);
		g_free (method_name);
		return FALSE;
	}

	gboolean res = g_hash_table_lookup (module_table, method_name) != NULL;
	// printf ("method %s -> %d\n", method_name, res);
	return res;
}