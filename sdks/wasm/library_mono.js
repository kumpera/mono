
var MonoSupportLib = {
	$MONO__postset: 'Module["pump_message"] = MONO.pump_message',
	$MONO: {
		pump_count: 0,
		timeout_queue: [],
		pump_message: function () {
			if (!this.mono_background_exec)
				this.mono_background_exec = Module.cwrap ("mono_background_exec", 'void', [ ]);
			while (MONO.timeout_queue.length > 0) {
				--MONO.pump_count;
				MONO.timeout_queue.shift()();
			}
			while (MONO.pump_count > 0) {
				--MONO.pump_count;
				this.mono_background_exec ();
			}
		},

		mono_wasm_get_call_stack: function() {
			if (!this.mono_wasm_current_bp_id)
				this.mono_wasm_current_bp_id = Module.cwrap ("mono_wasm_current_bp_id", 'number', [ ]);
			if (!this.mono_wasm_enum_frames)
				this.mono_wasm_enum_frames = Module.cwrap ("mono_wasm_enum_frames", 'void', [ ]);

			var bp_id = this.mono_wasm_current_bp_id ();
			this.active_frames = [];
			this.mono_wasm_enum_frames ();

			var the_frames = this.active_frames;
			this.active_frames = [];
			return {
				"breakpoint_id": bp_id,
				"frames": the_frames,
			};
		},

		mono_wasm_get_variables: function(scope, var_list) {
			if (!this.mono_wasm_get_var_info)
				this.mono_wasm_get_var_info = Module.cwrap ("mono_wasm_get_var_info", 'void', [ 'number', 'number']);

			//FIXME it would be more efficient to do a single call passing an array with var_list as argument instead
			this.var_info = [];
			for (var i in var_list)
				this.mono_wasm_get_var_info (scope, i);

			var res = this.var_info;
			this.var_info = []

			return res;
		},
	},

	mono_wasm_add_var: function(var_type, var_value) {
			MONO.var_info.push({
				type: Module.UTF8ToString (var_type),
				value: Module.UTF8ToString (var_value),
			});
	},

	mono_wasm_add_frame: function(il, method, mvid) {
		MONO.active_frames.push( {
			il_pos: il,
			method_token: method,
			assembly_mvid: Module.UTF8ToString (mvid)
		});
	},

	schedule_background_exec: function () {
		++MONO.pump_count;
		if (ENVIRONMENT_IS_WEB) {
			window.setTimeout (MONO.pump_message, 0);
		}
	},

	mono_set_timeout: function (timeout, id) {
		if (!this.mono_set_timeout_exec)
			this.mono_set_timeout_exec = Module.cwrap ("mono_set_timeout_exec", 'void', [ 'number' ]);
		if (ENVIRONMENT_IS_WEB) {
			window.setTimeout (function () {
				this.mono_set_timeout_exec (id);
			}, timeout);
		} else {
			++MONO.pump_count;
			MONO.timeout_queue.push(function() {
				this.mono_set_timeout_exec (id);
			})
		}
	},

	mono_wasm_fire_bp: function () {
		console.log ("mono_wasm_fire_bp");
		debugger;
	}
};

autoAddDeps(MonoSupportLib, '$MONO')
mergeInto(LibraryManager.library, MonoSupportLib)

