/******************************************************************************
 *
 * Name: acglobal.h - Declarations for global variables
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2014, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef __ACGLOBAL_H__
#define __ACGLOBAL_H__

/*
 * Ensure that the globals are actually defined and initialized only once.
 *
 * The use of these macros allows a single list of globals (here) in order
 * to simplify maintenance of the code.
 */
#ifdef DEFINE_ACPI_GLOBALS
#define ACPI_GLOBAL(type,name) \
	extern type name; \
	type name

#define ACPI_INIT_GLOBAL(type,name,value) \
	type name=value

#else
#define ACPI_GLOBAL(type,name) \
	extern type name

#define ACPI_INIT_GLOBAL(type,name,value) \
	extern type name
#endif

#ifdef DEFINE_ACPI_GLOBALS

/* Public globals, available from outside ACPICA subsystem */

/*****************************************************************************
 *
 * Runtime configuration (static defaults that can be overriden at runtime)
 *
 ****************************************************************************/

/*
 * Enable "slack" in the AML interpreter?  Default is FALSE, and the
 * interpreter strictly follows the ACPI specification. Setting to TRUE
 * allows the interpreter to ignore certain errors and/or bad AML constructs.
 *
 * Currently, these features are enabled by this flag:
 *
 * 1) Allow "implicit return" of last value in a control method
 * 2) Allow access beyond the end of an operation region
 * 3) Allow access to uninitialized locals/args (auto-init to integer 0)
 * 4) Allow ANY object type to be a source operand for the Store() operator
 * 5) Allow unresolved references (invalid target name) in package objects
 * 6) Enable warning messages for behavior that is not ACPI spec compliant
 */
ACPI_INIT_GLOBAL(u8, acpi_gbl_enable_interpreter_slack, FALSE);

/*
 * Automatically serialize all methods that create named objects? Default
 * is TRUE, meaning that all non_serialized methods are scanned once at
 * table load time to determine those that create named objects. Methods
 * that create named objects are marked Serialized in order to prevent
 * possible run-time problems if they are entered by more than one thread.
 */
ACPI_INIT_GLOBAL(u8, acpi_gbl_auto_serialize_methods, TRUE);

/*
 * Create the predefined _OSI method in the namespace? Default is TRUE
 * because ACPICA is fully compatible with other ACPI implementations.
 * Changing this will revert ACPICA (and machine ASL) to pre-OSI behavior.
 */
ACPI_INIT_GLOBAL(u8, acpi_gbl_create_osi_method, TRUE);

/*
 * Optionally use default values for the ACPI register widths. Set this to
 * TRUE to use the defaults, if an FADT contains incorrect widths/lengths.
 */
ACPI_INIT_GLOBAL(u8, acpi_gbl_use_default_register_widths, TRUE);

/*
 * Optionally enable output from the AML Debug Object.
 */
ACPI_INIT_GLOBAL(u8, acpi_gbl_enable_aml_debug_object, FALSE);

/*
 * Optionally copy the entire DSDT to local memory (instead of simply
 * mapping it.) There are some BIOSs that corrupt or replace the original
 * DSDT, creating the need for this option. Default is FALSE, do not copy
 * the DSDT.
 */
ACPI_INIT_GLOBAL(u8, acpi_gbl_copy_dsdt_locally, FALSE);

/*
 * Optionally ignore an XSDT if present and use the RSDT instead.
 * Although the ACPI specification requires that an XSDT be used instead
 * of the RSDT, the XSDT has been found to be corrupt or ill-formed on
 * some machines. Default behavior is to use the XSDT if present.
 */
ACPI_INIT_GLOBAL(u8, acpi_gbl_do_not_use_xsdt, FALSE);

/*
 * Optionally use 32-bit FADT addresses if and when there is a conflict
 * (address mismatch) between the 32-bit and 64-bit versions of the
 * address. Although ACPICA adheres to the ACPI specification which
 * requires the use of the corresponding 64-bit address if it is non-zero,
 * some machines have been found to have a corrupted non-zero 64-bit
 * address. Default is FALSE, do not favor the 32-bit addresses.
 */
ACPI_INIT_GLOBAL(u8, acpi_gbl_use32_bit_fadt_addresses, FALSE);

/*
 * Optionally truncate I/O addresses to 16 bits. Provides compatibility
 * with other ACPI implementations. NOTE: During ACPICA initialization,
 * this value is set to TRUE if any Windows OSI strings have been
 * requested by the BIOS.
 */
ACPI_INIT_GLOBAL(u8, acpi_gbl_truncate_io_addresses, FALSE);

/*
 * Disable runtime checking and repair of values returned by control methods.
 * Use only if the repair is causing a problem on a particular machine.
 */
ACPI_INIT_GLOBAL(u8, acpi_gbl_disable_auto_repair, FALSE);

/*
 * Optionally do not load any SSDTs from the RSDT/XSDT during initialization.
 * This can be useful for debugging ACPI problems on some machines.
 */
ACPI_INIT_GLOBAL(u8, acpi_gbl_disable_ssdt_table_load, FALSE);

/*
 * We keep track of the latest version of Windows that has been requested by
 * the BIOS.
 */
ACPI_INIT_GLOBAL(u8, acpi_gbl_osi_data, 0);

#endif				/* DEFINE_ACPI_GLOBALS */

/*****************************************************************************
 *
 * ACPI Table globals
 *
 ****************************************************************************/

/*
 * Master list of all ACPI tables that were found in the RSDT/XSDT.
 */
ACPI_GLOBAL(struct acpi_table_list, acpi_gbl_root_table_list);

/* DSDT information. Used to check for DSDT corruption */

ACPI_GLOBAL(struct acpi_table_header *, acpi_gbl_DSDT);
ACPI_GLOBAL(struct acpi_table_header, acpi_gbl_original_dsdt_header);

#if (!ACPI_REDUCED_HARDWARE)
ACPI_GLOBAL(struct acpi_table_facs *, acpi_gbl_FACS);

#endif				/* !ACPI_REDUCED_HARDWARE */

/* These addresses are calculated from the FADT Event Block addresses */

ACPI_GLOBAL(struct acpi_generic_address, acpi_gbl_xpm1a_status);
ACPI_GLOBAL(struct acpi_generic_address, acpi_gbl_xpm1a_enable);

ACPI_GLOBAL(struct acpi_generic_address, acpi_gbl_xpm1b_status);
ACPI_GLOBAL(struct acpi_generic_address, acpi_gbl_xpm1b_enable);

/*
 * Handle both ACPI 1.0 and ACPI 2.0+ Integer widths. The integer width is
 * determined by the revision of the DSDT: If the DSDT revision is less than
 * 2, use only the lower 32 bits of the internal 64-bit Integer.
 */
ACPI_GLOBAL(u8, acpi_gbl_integer_bit_width);
ACPI_GLOBAL(u8, acpi_gbl_integer_byte_width);
ACPI_GLOBAL(u8, acpi_gbl_integer_nybble_width);

/*****************************************************************************
 *
 * Mutual exclusion within ACPICA subsystem
 *
 ****************************************************************************/

/*
 * Predefined mutex objects. This array contains the
 * actual OS mutex handles, indexed by the local ACPI_MUTEX_HANDLEs.
 * (The table maps local handles to the real OS handles)
 */
ACPI_GLOBAL(struct acpi_mutex_info, acpi_gbl_mutex_info[ACPI_NUM_MUTEX]);

/*
 * Global lock mutex is an actual AML mutex object
 * Global lock semaphore works in conjunction with the actual global lock
 * Global lock spinlock is used for "pending" handshake
 */
ACPI_GLOBAL(union acpi_operand_object *, acpi_gbl_global_lock_mutex);
ACPI_GLOBAL(acpi_semaphore, acpi_gbl_global_lock_semaphore);
ACPI_GLOBAL(acpi_spinlock, acpi_gbl_global_lock_pending_lock);
ACPI_GLOBAL(u16, acpi_gbl_global_lock_handle);
ACPI_GLOBAL(u8, acpi_gbl_global_lock_acquired);
ACPI_GLOBAL(u8, acpi_gbl_global_lock_present);
ACPI_GLOBAL(u8, acpi_gbl_global_lock_pending);

/*
 * Spinlocks are used for interfaces that can be possibly called at
 * interrupt level
 */
ACPI_GLOBAL(acpi_spinlock, acpi_gbl_gpe_lock);	/* For GPE data structs and registers */
ACPI_GLOBAL(acpi_spinlock, acpi_gbl_hardware_lock);	/* For ACPI H/W except GPE registers */
ACPI_GLOBAL(acpi_spinlock, acpi_gbl_reference_count_lock);

/* Mutex for _OSI support */

ACPI_GLOBAL(acpi_mutex, acpi_gbl_osi_mutex);

/* Reader/Writer lock is used for namespace walk and dynamic table unload */

ACPI_GLOBAL(struct acpi_rw_lock, acpi_gbl_namespace_rw_lock);

/*****************************************************************************
 *
 * Miscellaneous globals
 *
 ****************************************************************************/

/* Object caches */

ACPI_GLOBAL(acpi_cache_t *, acpi_gbl_namespace_cache);
ACPI_GLOBAL(acpi_cache_t *, acpi_gbl_state_cache);
ACPI_GLOBAL(acpi_cache_t *, acpi_gbl_ps_node_cache);
ACPI_GLOBAL(acpi_cache_t *, acpi_gbl_ps_node_ext_cache);
ACPI_GLOBAL(acpi_cache_t *, acpi_gbl_operand_cache);

/* System */

ACPI_INIT_GLOBAL(u32, acpi_gbl_startup_flags, 0);
ACPI_INIT_GLOBAL(u8, acpi_gbl_shutdown, TRUE);

/* Global handlers */

ACPI_GLOBAL(struct acpi_global_notify_handler, acpi_gbl_global_notify[2]);
ACPI_GLOBAL(acpi_exception_handler, acpi_gbl_exception_handler);
ACPI_GLOBAL(acpi_init_handler, acpi_gbl_init_handler);
ACPI_GLOBAL(acpi_table_handler, acpi_gbl_table_handler);
ACPI_GLOBAL(void *, acpi_gbl_table_handler_context);
ACPI_GLOBAL(struct acpi_walk_state *, acpi_gbl_breakpoint_walk);
ACPI_GLOBAL(acpi_interface_handler, acpi_gbl_interface_handler);
ACPI_GLOBAL(struct acpi_sci_handler_info *, acpi_gbl_sci_handler_list);

/* Owner ID support */

ACPI_GLOBAL(u32, acpi_gbl_owner_id_mask[ACPI_NUM_OWNERID_MASKS]);
ACPI_GLOBAL(u8, acpi_gbl_last_owner_id_index);
ACPI_GLOBAL(u8, acpi_gbl_next_owner_id_offset);

/* Initialization sequencing */

ACPI_GLOBAL(u8, acpi_gbl_reg_methods_executed);

/* Misc */

ACPI_GLOBAL(u32, acpi_gbl_original_mode);
ACPI_GLOBAL(u32, acpi_gbl_rsdp_original_location);
ACPI_GLOBAL(u32, acpi_gbl_ns_lookup_count);
ACPI_GLOBAL(u32, acpi_gbl_ps_find_count);
ACPI_GLOBAL(u16, acpi_gbl_pm1_enable_register_save);
ACPI_GLOBAL(u8, acpi_gbl_debugger_configuration);
ACPI_GLOBAL(u8, acpi_gbl_step_to_next_call);
ACPI_GLOBAL(u8, acpi_gbl_acpi_hardware_present);
ACPI_GLOBAL(u8, acpi_gbl_events_initialized);
ACPI_GLOBAL(struct acpi_interface_info *, acpi_gbl_supported_interfaces);
ACPI_GLOBAL(struct acpi_address_range *,
	    acpi_gbl_address_range_list[ACPI_ADDRESS_RANGE_MAX]);

/* Other miscellaneous, declared and initialized in utglobal */

extern const char *acpi_gbl_sleep_state_names[ACPI_S_STATE_COUNT];
extern const char *acpi_gbl_lowest_dstate_names[ACPI_NUM_sx_w_METHODS];
extern const char *acpi_gbl_highest_dstate_names[ACPI_NUM_sx_d_METHODS];
extern const char *acpi_gbl_region_types[ACPI_NUM_PREDEFINED_REGIONS];
extern const struct acpi_opcode_info acpi_gbl_aml_op_info[AML_NUM_OPCODES];

#ifdef ACPI_DBG_TRACK_ALLOCATIONS

/* Lists for tracking memory allocations (debug only) */

ACPI_GLOBAL(struct acpi_memory_list *, acpi_gbl_global_list);
ACPI_GLOBAL(struct acpi_memory_list *, acpi_gbl_ns_node_list);
ACPI_GLOBAL(u8, acpi_gbl_display_final_mem_stats);
ACPI_GLOBAL(u8, acpi_gbl_disable_mem_tracking);
#endif

/*****************************************************************************
 *
 * Namespace globals
 *
 ****************************************************************************/

#if !defined (ACPI_NO_METHOD_EXECUTION) || defined (ACPI_CONSTANT_EVAL_ONLY)
#define NUM_PREDEFINED_NAMES            10
#else
#define NUM_PREDEFINED_NAMES            9
#endif

ACPI_GLOBAL(struct acpi_namespace_node, acpi_gbl_root_node_struct);
ACPI_GLOBAL(struct acpi_namespace_node *, acpi_gbl_root_node);
ACPI_GLOBAL(struct acpi_namespace_node *, acpi_gbl_fadt_gpe_device);
ACPI_GLOBAL(union acpi_operand_object *, acpi_gbl_module_code_list);

extern const u8 acpi_gbl_ns_properties[ACPI_NUM_NS_TYPES];
extern const struct acpi_predefined_names
    acpi_gbl_pre_defined_names[NUM_PREDEFINED_NAMES];

#ifdef ACPI_DEBUG_OUTPUT
ACPI_GLOBAL(u32, acpi_gbl_current_node_count);
ACPI_GLOBAL(u32, acpi_gbl_current_node_size);
ACPI_GLOBAL(u32, acpi_gbl_max_concurrent_node_count);
ACPI_GLOBAL(acpi_size *, acpi_gbl_entry_stack_pointer);
ACPI_GLOBAL(acpi_size *, acpi_gbl_lowest_stack_pointer);
ACPI_GLOBAL(u32, acpi_gbl_deepest_nesting);
ACPI_INIT_GLOBAL(u32, acpi_gbl_nesting_level, 0);
#endif

/*****************************************************************************
 *
 * Interpreter globals
 *
 ****************************************************************************/

ACPI_GLOBAL(struct acpi_thread_state *, acpi_gbl_current_walk_list);

/* Control method single step flag */

ACPI_GLOBAL(u8, acpi_gbl_cm_single_step);

/*****************************************************************************
 *
 * Hardware globals
 *
 ****************************************************************************/

extern struct acpi_bit_register_info
    acpi_gbl_bit_register_info[ACPI_NUM_BITREG];

ACPI_GLOBAL(u8, acpi_gbl_sleep_type_a);
ACPI_GLOBAL(u8, acpi_gbl_sleep_type_b);

/*****************************************************************************
 *
 * Event and GPE globals
 *
 ****************************************************************************/

#if (!ACPI_REDUCED_HARDWARE)

ACPI_GLOBAL(u8, acpi_gbl_all_gpes_initialized);
ACPI_GLOBAL(struct acpi_gpe_xrupt_info *, acpi_gbl_gpe_xrupt_list_head);
ACPI_GLOBAL(struct acpi_gpe_block_info *,
	    acpi_gbl_gpe_fadt_blocks[ACPI_MAX_GPE_BLOCKS]);
ACPI_GLOBAL(acpi_gbl_event_handler, acpi_gbl_global_event_handler);
ACPI_GLOBAL(void *, acpi_gbl_global_event_handler_context);
ACPI_GLOBAL(struct acpi_fixed_event_handler,
	    acpi_gbl_fixed_event_handlers[ACPI_NUM_FIXED_EVENTS]);

extern struct acpi_fixed_event_info
    acpi_gbl_fixed_event_info[ACPI_NUM_FIXED_EVENTS];

#endif				/* !ACPI_REDUCED_HARDWARE */

/*****************************************************************************
 *
 * Debug support
 *
 ****************************************************************************/

/* Event counters */

ACPI_GLOBAL(u32, acpi_method_count);
ACPI_GLOBAL(u32, acpi_gpe_count);
ACPI_GLOBAL(u32, acpi_sci_count);
ACPI_GLOBAL(u32, acpi_fixed_event_count[ACPI_NUM_FIXED_EVENTS]);

/* Support for dynamic control method tracing mechanism */

ACPI_GLOBAL(u32, acpi_gbl_original_dbg_level);
ACPI_GLOBAL(u32, acpi_gbl_original_dbg_layer);
ACPI_GLOBAL(u32, acpi_gbl_trace_dbg_level);
ACPI_GLOBAL(u32, acpi_gbl_trace_dbg_layer);

/*****************************************************************************
 *
 * Debugger and Disassembler globals
 *
 ****************************************************************************/

ACPI_GLOBAL(u8, acpi_gbl_db_output_flags);

#ifdef ACPI_DISASSEMBLER

/* Do not disassemble buffers to resource descriptors */

ACPI_INIT_GLOBAL(u8, acpi_gbl_no_resource_disassembly, FALSE);
ACPI_INIT_GLOBAL(u8, acpi_gbl_ignore_noop_operator, FALSE);

ACPI_GLOBAL(u8, acpi_gbl_db_opt_disasm);
ACPI_GLOBAL(u8, acpi_gbl_db_opt_verbose);
ACPI_GLOBAL(u8, acpi_gbl_num_external_methods);
ACPI_GLOBAL(u32, acpi_gbl_resolved_external_methods);
ACPI_GLOBAL(struct acpi_external_list *, acpi_gbl_external_list);
ACPI_GLOBAL(struct acpi_external_file *, acpi_gbl_external_file_list);
#endif

#ifdef ACPI_DEBUGGER

ACPI_INIT_GLOBAL(u8, acpi_gbl_db_terminate_threads, FALSE);
ACPI_INIT_GLOBAL(u8, acpi_gbl_abort_method, FALSE);
ACPI_INIT_GLOBAL(u8, acpi_gbl_method_executing, FALSE);

ACPI_GLOBAL(u8, acpi_gbl_db_opt_tables);
ACPI_GLOBAL(u8, acpi_gbl_db_opt_stats);
ACPI_GLOBAL(u8, acpi_gbl_db_opt_ini_methods);
ACPI_GLOBAL(u8, acpi_gbl_db_opt_no_region_support);
ACPI_GLOBAL(u8, acpi_gbl_db_output_to_file);
ACPI_GLOBAL(char *, acpi_gbl_db_buffer);
ACPI_GLOBAL(char *, acpi_gbl_db_filename);
ACPI_GLOBAL(u32, acpi_gbl_db_debug_level);
ACPI_GLOBAL(u32, acpi_gbl_db_console_debug_level);
ACPI_GLOBAL(struct acpi_namespace_node *, acpi_gbl_db_scope_node);

ACPI_GLOBAL(char *, acpi_gbl_db_args[ACPI_DEBUGGER_MAX_ARGS]);
ACPI_GLOBAL(acpi_object_type, acpi_gbl_db_arg_types[ACPI_DEBUGGER_MAX_ARGS]);

/* These buffers should all be the same size */

ACPI_GLOBAL(char, acpi_gbl_db_line_buf[ACPI_DB_LINE_BUFFER_SIZE]);
ACPI_GLOBAL(char, acpi_gbl_db_parsed_buf[ACPI_DB_LINE_BUFFER_SIZE]);
ACPI_GLOBAL(char, acpi_gbl_db_scope_buf[ACPI_DB_LINE_BUFFER_SIZE]);
ACPI_GLOBAL(char, acpi_gbl_db_debug_filename[ACPI_DB_LINE_BUFFER_SIZE]);

/*
 * Statistic globals
 */
ACPI_GLOBAL(u16, acpi_gbl_obj_type_count[ACPI_TYPE_NS_NODE_MAX + 1]);
ACPI_GLOBAL(u16, acpi_gbl_node_type_count[ACPI_TYPE_NS_NODE_MAX + 1]);
ACPI_GLOBAL(u16, acpi_gbl_obj_type_count_misc);
ACPI_GLOBAL(u16, acpi_gbl_node_type_count_misc);
ACPI_GLOBAL(u32, acpi_gbl_num_nodes);
ACPI_GLOBAL(u32, acpi_gbl_num_objects);

ACPI_GLOBAL(u32, acpi_gbl_size_of_parse_tree);
ACPI_GLOBAL(u32, acpi_gbl_size_of_method_trees);
ACPI_GLOBAL(u32, acpi_gbl_size_of_node_entries);
ACPI_GLOBAL(u32, acpi_gbl_size_of_acpi_objects);

#endif				/* ACPI_DEBUGGER */

/*****************************************************************************
 *
 * Application globals
 *
 ****************************************************************************/

#ifdef ACPI_APPLICATION

ACPI_INIT_GLOBAL(ACPI_FILE, acpi_gbl_debug_file, NULL);

#endif				/* ACPI_APPLICATION */

/*****************************************************************************
 *
 * Info/help support
 *
 ****************************************************************************/

extern const struct ah_predefined_name asl_predefined_info[];

#endif				/* __ACGLOBAL_H__ */
