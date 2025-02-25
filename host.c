#include <sys/stat.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <wasm.h>
#include <wasmtime.h>

/// Compile a WASM program (stored in a byte array)
static wasmtime_module_t*	compile_wasm(wasm_engine_t *, wasm_byte_vec_t);

/// Print a value passed to or from WASM
static void			print_value(const wasmtime_val_t *);

/// Read a file into a WASM byte vector
static wasm_byte_vec_t		read_file(const char *filename);


int
main(int argc, char *argv[])
{
	// Check user input
	if (argc != 2)
	{
		fprintf(stderr, "Usage:  %s <WASM file>\n", argv[0]);
		return 1;
	}

	// Initialize wasmtime engine, load the module's bytes and compile them
	wasm_engine_t *engine = wasm_engine_new();
	assert(engine && "Failed to initialize WASM engine");

	wasm_byte_vec_t wasm_file = read_file(argv[1]);
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
	wasmtime_instance_new(context, module, imports, 0, &instance, &trap);

	if (error != NULL || trap != NULL)
	{
		fprintf(stderr, "Failed to create module instance\n");
		return 1;
	}

	// Find the function exported from the module
	wasmtime_extern_t fn_to_run;
	bool ok = wasmtime_instance_export_get(context,
	                                       &instance,
	                                       "add",
	                                       3,
	                                       &fn_to_run);

	assert(ok && "Failed to get add function from WASM module");
	assert(fn_to_run.kind == WASMTIME_EXTERN_FUNC);

	// Call it!
	wasmtime_val_t args[2];
	args[0].kind = WASMTIME_I32;
	args[0].of.i32 = 2;
	args[1].kind = WASMTIME_I32;
	args[1].of.i32 = 2;

	printf("Calling add(");
	print_value(&args[0]);
	printf(", ");
	print_value(&args[1]);
	printf(")...\n");

	wasmtime_val_t result;
	error = wasmtime_func_call(context,
	                           &fn_to_run.of.func,
	                           args,
	                           2,
	                           &result,
	                           1,
	                           &trap);

	if (error || trap)
	{
		fprintf(stderr, "Failed to run function\n");
		return 1;
	}

	printf("Result: ");
	print_value(&result);
	printf("\n");

	// Clean up
	wasmtime_module_delete(module);
	wasmtime_store_delete(store);
	wasm_engine_delete(engine);

	return 0;
}


static wasmtime_module_t*
compile_wasm(wasm_engine_t *engine, wasm_byte_vec_t program)
{
	wasmtime_module_t *module = NULL;
	wasmtime_error_t *error = wasmtime_module_new(engine,
	                                              (uint8_t*) program.data,
	                                              program.size,
	                                              &module);

	if (error != NULL)
	{
		fprintf(stderr, "Failed to compile WASM module\n");
		return NULL;
	}

	return module;
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
