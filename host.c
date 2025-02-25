#include <sys/stat.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wasm.h>
#include <wasmtime.h>

/// Call a WASM function.
static wasmtime_val_t		call(wasmtime_context_t *ctx,
                                     wasmtime_func_t *fn,
                                     const char *fn_name,
                                     wasmtime_val_t *args,
                                     size_t arglen);

/// Check whether an error has occurred (aborts if it has).
static void			check_error(wasmtime_error_t *error,
                                            wasm_trap_t *trap,
                                            const char *user_msg);

/// Compile a WASM program (stored in a byte array)
static wasmtime_module_t*	compile_wasm(wasm_engine_t *, wasm_byte_vec_t);

/// Get a reference to something exported from a running module instance.
static wasmtime_extern_t	get_export(wasmtime_context_t*,
                                           wasmtime_instance_t*,
                                           const char *name);

/// Get a reference to a function exported from a running module instance.
static wasmtime_func_t		get_fn(wasmtime_context_t*,
                                       wasmtime_instance_t*,
                                       const char *name);

/// Get a reference to an instance's linear memory.
static wasmtime_memory_t	get_memory(wasmtime_context_t*,
                                           wasmtime_instance_t*);

/// Print bytes from a memory offset in a WASM linear memory.
static void			print_memory(wasmtime_context_t*,
                                             wasmtime_memory_t *memory,
                                             size_t start, size_t len);

/// Print a value passed to or from WASM
static void			print_value(const wasmtime_val_t *);

/// Read a file into a WASM byte vector
static wasm_byte_vec_t		read_file(const char *filename);

/// Functions we are pre-programmed to handle
enum ExampleFunction {
	FN_ADD,
	FN_TRANSLATE,
};


int
main(int argc, char *argv[])
{
	// Check user input
	if (argc != 2)
	{
		fprintf(stderr, "Usage:  %s <function>\n", argv[0]);
		return 1;
	}

	enum ExampleFunction fn;

	if (strcmp(argv[1], "add") == 0) {
		fn = FN_ADD;
	} else if (strcmp(argv[1], "translate") == 0) {
		fn = FN_TRANSLATE;
	} else {
		fprintf(stderr, "Function must be 'add' or 'translate'\n");
		return 1;
	}

	char filename[128];
	sprintf(filename, "examples/%s.wasm", argv[1]);

	// Initialize wasmtime engine, load the module's bytes and compile them
	wasm_engine_t *engine = wasm_engine_new();
	assert(engine && "Failed to initialize WASM engine");

	wasm_byte_vec_t wasm_file = read_file(filename);
	wasmtime_module_t *module = compile_wasm(engine, wasm_file);
	assert(module && "Failed to compile WASM module");

	wasm_byte_vec_delete(&wasm_file);

	//
	// Run some code!
	//

	// Create store for module instances
	wasmtime_store_t *store = wasmtime_store_new(engine, NULL, NULL);
	assert(store && "Failed to create wasmtime module store");

	wasmtime_context_t *context = wasmtime_store_context(store);
	assert(context && "Failed to create module store context");

	// Create an instance of our module
	wasmtime_error_t *error = NULL;
	wasm_trap_t *trap = NULL;
	wasmtime_instance_t instance;
	wasmtime_extern_t imports[0];  // we don't provide any imports (now)
				       //
	wasmtime_instance_new(context, module, imports, 0, &instance, &trap);
	check_error(error, trap, "Failed to create module instance");

	// Find the function exported from the module
	const char *fn_name;
	wasmtime_memory_t memory;
	wasmtime_val_t args[2];
	args[0].kind = WASMTIME_I32;
	args[1].kind = WASMTIME_I32;

	switch (fn)
	{
		case FN_ADD:
			fn_name = "add";

			// The add function takes two integer arguments
			args[0].of.i32 = 2;
			args[1].of.i32 = 2;
			break;

		case FN_TRANSLATE:
			fn_name = "translate";

			// Set some bytes in the instance's linear memory.
			memory = get_memory(context, &instance);
			strcpy((char*) wasmtime_memory_data(context, &memory),
			       "Hello, world!");

			// The translate argument takes a buffer pointer
			// (within the linear memory) and a length
			args[0].of.i32 = 0;
			args[1].of.i32 = 128;

			break;
	}

	if (fn == FN_TRANSLATE)
	{
		printf("Initial memory state:\n");
		print_memory(context, &memory, 0, 128);
		printf("\n");
	}


	// Call it!
	wasmtime_func_t fn_to_run = get_fn(context, &instance, fn_name);
	call(context, &fn_to_run, fn_name, args, 2);

	if (fn == FN_TRANSLATE)
	{
		printf("\nFinal memory state:\n");
		print_memory(context, &memory, 0, 128);
	}

	// Clean up
	wasmtime_module_delete(module);
	wasmtime_store_delete(store);
	wasm_engine_delete(engine);

	return 0;
}


static wasmtime_val_t
call(wasmtime_context_t *ctx, wasmtime_func_t *fn, const char *fn_name,
     wasmtime_val_t *args, size_t arglen)
{
	printf("Calling function %s with arguments:\n", fn_name);
	for (size_t i = 0; i < arglen; i++)
	{
		printf("%8ld: ", i);
		print_value(&args[i]);
		printf("\n");
	}
	printf("\n");

	wasmtime_error_t *error = NULL;
	wasm_trap_t *trap = NULL;
	wasmtime_val_t result;

	error = wasmtime_func_call(ctx, fn, args, arglen, &result, 1, &trap);
	check_error(error, trap, "Failed to run function");

	printf("Result: ");
	print_value(&result);
	printf("\n");

	return result;
}


static void
check_error(wasmtime_error_t *error, wasm_trap_t *trap, const char *user_msg)
{
	wasm_byte_vec_t msg;
	const char *kind = "";

	if (error)
	{
		kind = "error";
		wasmtime_error_message(error, &msg);
		wasmtime_error_delete(error);
	}
	else if (trap)
	{
		kind = "trap";
		wasm_trap_message(trap, &msg);
		wasm_trap_delete(trap);
	}
	else
	{
		return;
	}

	wasm_byte_vec_delete(&msg);

	fprintf(stderr, "%s: %s %.*s\n", user_msg, kind,
	        (int) msg.size, msg.data);
	exit(1);
}


static wasmtime_module_t*
compile_wasm(wasm_engine_t *engine, wasm_byte_vec_t program)
{
	wasmtime_module_t *module = NULL;
	wasmtime_error_t *error = wasmtime_module_new(engine,
	                                              (uint8_t*) program.data,
	                                              program.size,
	                                              &module);

	check_error(error, NULL, "Failed to compile WASM module");
	return module;
}


static wasmtime_extern_t
get_export(wasmtime_context_t *context,
           wasmtime_instance_t *instance,
           const char *name)
{
	wasmtime_extern_t fn_to_run;
	bool ok = wasmtime_instance_export_get(context,
	                                       instance,
	                                       name,
	                                       strlen(name),
	                                       &fn_to_run);

	if (!ok)
	{
		fprintf(stderr, "Failed to get %s from module\n", name);
		exit(1);
	}

	return fn_to_run;
}


static wasmtime_func_t
get_fn(wasmtime_context_t *context,
       wasmtime_instance_t *instance,
       const char *name)
{
	wasmtime_extern_t e = get_export(context, instance, name);
	assert(e.kind == WASMTIME_EXTERN_FUNC);
	return e.of.func;
}


static wasmtime_memory_t
get_memory(wasmtime_context_t *context, wasmtime_instance_t *instance)
{
	wasmtime_extern_t e = get_export(context, instance, "memory");
	assert(e.kind == WASMTIME_EXTERN_MEMORY);
	return e.of.memory;
}


static void
print_memory(wasmtime_context_t *context, wasmtime_memory_t *memory,
             size_t start, size_t len)
{
	for (int i = start; i < start + len; i++)
	{
		printf("%02x ", wasmtime_memory_data(context, memory)[i]);
		if (i % 16 == 7)
		{
			printf(" ");
		}
		else if (i % 16 == 15)
		{
			printf("\n");
		}
	}
}


static void
print_value(const wasmtime_val_t *vp)
{
	switch (vp->kind)
	{
		case WASMTIME_ANYREF:
			printf("anyref %ld", vp->of.anyref.store_id);
			break;

		case WASMTIME_EXTERNREF:
			printf("externref %ld", vp->of.externref.store_id);
			break;

		case WASMTIME_FUNCREF:
			printf("funcref %ld", vp->of.funcref.store_id);
			break;

		case WASMTIME_F32:
			printf("f32 %f", vp->of.f32);
			break;

		case WASMTIME_F64:
			printf("f64 %f", vp->of.f64);
			break;

		case WASMTIME_I32:
			printf("i32 %d (0x%x)", vp->of.i32, vp->of.i32);
			break;

		case WASMTIME_I64:
			printf("i64 %ld (0x%lx)", vp->of.i64, vp->of.i64);
			break;

		case WASMTIME_V128:
			printf("v128 (packed SIMD data)");
			break;
	}
}


static wasm_byte_vec_t
read_file(const char *filename)
{
	// Open the file and check its length
	int fd = open(filename, O_RDONLY);
	if (fd < 0)
	{
		err(-1, "Failed to open '%s'", filename);
	}

	struct stat s;
	if (fstat(fd, &s) != 0) {
		err(-1, "Failed to stat(2) '%s'", filename);
	}

	// Convert opened file into a libc FILE and read into a byte vector
	wasm_byte_vec_t data;
	wasm_byte_vec_new_uninitialized(&data, s.st_size);

	FILE *f = fdopen(fd, "r");
	if (fread(data.data, s.st_size, 1, f) != 1) {
		err(-1, "Failed to read from '%s'", filename);
	}

	return data;
}
