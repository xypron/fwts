/*
 * Copyright (C) 2006, Intel Corporation
 * Copyright (C) 2010 Canonical
 * 
 * This file was originally part of the Linux-ready Firmware Developer Kit
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "fwts.h"

#ifdef FWTS_ARCH_INTEL

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

static void acpiinfo_check(fwts_framework *fw, char *line, char *prevline, void *private, int *errors)
{
	if (strstr(line, "ACPI: Subsystem revision")!=NULL) {
		char *version = strstr(line,"sion ");
		if (version) {
			version+=5;
			fwts_log_info(fw, "Linux ACPI interpreter version %s.", version);
		}
	}

	if (strstr(line, "ACPI: DSDT")!=NULL) {
		char *vendor = "Unknown";
		if (strstr(line,"MSFT"))
			vendor="Microsoft";
		if (strstr(line,"INTL"))
			vendor="Intel";
		fwts_log_info(fw,"DSDT was compiled by the %s AML compiler.", vendor);
	}

	if (strstr(line, "Disabling IRQ")!=NULL && prevline && strstr(prevline,"acpi_irq")) {
		fwts_failed(fw, "ACPI interrupt got stuck: level triggered?");
		fwts_log_info_verbatum(fw,"%s", line);
	}

	if (prevline && strstr(prevline, "*** Error: Return object type is incorrect")) {
		fwts_failed_high(fw, "Return object type is incorrect: %s.", line);
		fwts_log_info_verbatum(fw,"%s", line);
	}

	if (strstr(line,"ACPI: acpi_ec_space_handler: bit_width is 32, should be")) {
		fwts_failed_low(fw, "Embedded controller bit_width is incorrect: %s.", line);
		fwts_log_info_verbatum(fw,"%s", line);
	}

	if (strstr(line,"acpi_ec_space_handler: bit_width should be")) {
		fwts_failed_low(fw, "Embedded controller bit_width is incorrect: %s.", line);
		fwts_log_info_verbatum(fw,"%s", line);
	}

	if (strstr(line,"Warning: acpi_table_parse(ACPI_SRAT) returned 0!")) {
		fwts_failed(fw, "SRAT table cannot be found");
		fwts_log_info_verbatum(fw,"%s", line);
	}

	if (strstr(line,"Warning: acpi_table_parse(ACPI_SLIT) returned 0!")) {
		fwts_failed(fw, "SLIT table cannot be found");
		fwts_log_info_verbatum(fw,"%s", line);
	}
		
	if (strstr(line, "WARNING: No sibling found for CPU")) {
		fwts_failed(fw, "Hyperthreading CPU enumeration fails.");
		fwts_log_info_verbatum(fw,"%s", line);
	}

	if (prevline && strstr(line, ">>> ERROR: Invalid checksum") && strlen(prevline)>11) {
		char tmp[4096];
		strncpy(tmp, prevline, sizeof(tmp));
		tmp[11] = '\0';
		fwts_failed_high(fw, "ACPI table %s has an invalid checksum.", tmp+6);
		fwts_log_info_verbatum(fw,"%s", line);
	}

	if (strstr(line, "MP-BIOS bug: 8254 timer not connected to IO-APIC")) {
		fwts_failed_high(fw, "8254 timer not connected to IO-APIC: %s.", line);
		fwts_log_info_verbatum(fw,"%s", line);
	}

	if (strstr(line, "ACPI: PCI Interrupt Link") && strstr(line, " disabled and referenced, BIOS bug.")) {
		fwts_failed_high(fw, line);
		fwts_log_info_verbatum(fw,"%s", line);
	}

	if (strstr(line, "*** Warning Inconsistent FADT length") && strstr(line, "using FADT V1.0 portion of table")) {
		fwts_failed_high(fw, "FADT table claims to be of higher revision than it is.");
		fwts_log_info_verbatum(fw,"%s", line);
	}

	if (strstr(line, "thermal_get_trip_point: Invalid active threshold")) {
		fwts_failed_critical(fw, "_AC0 thermal trip point is invalid.");
		fwts_log_info_verbatum(fw,"%s", line);
	}

	if (strstr(line, "MMCONFIG has no entries")) {
		fwts_failed_high(fw, "The MCFG table has no entries!");
		fwts_log_info_verbatum(fw,"%s", line);
	}

	if (strstr(line, "MMCONFIG not in low 4GB of memory")) {
		fwts_failed(fw, "The MCFG table entries are not in the lower 4Gb of RAM.");
		fwts_log_info_verbatum(fw,"%s", line);
	}

	if (strstr(line, "pcie_portdrv_probe->Dev") && strstr(line, "has invalid IRQ. Check vendor BIOS")) {
		fwts_failed_high(fw, "PCI Express port driver reports an invalid IRQ.");
		fwts_log_info_verbatum(fw,"%s", line);
	}
		
	if (strstr(line, "OCHI: BIOS handoff failed (BIOS bug")) {
		fwts_failed(fw, "OHCI BIOS emulation handoff failed.");
		fwts_log_info_verbatum(fw,"%s", line);
		fwts_advice(fw, "Generally this means that the EHCI driver was unable to take "
			  	"control of the USB controller away from the BIOS. "
				"Disabling USB legacy mode in the BIOS may help.");
	}
	if (strstr(line, "EHCI: BIOS handoff failed (BIOS bug")) {
		fwts_failed(fw, "EHCI BIOS emulation handoff failed.");
		fwts_log_info_verbatum(fw,"%s", line);
		fwts_advice(fw, "Generally this means that the EHCI driver was unable to take "
				"control of the USB controller away from the BIOS. "
				"Disabling USB legacy mode in the BIOS may help.");
	}
}



static char *acpiinfo_headline(void)
{
	return "General ACPI information check.";
}

static fwts_list *klog;

static int acpiinfo_init(fwts_framework *fw)
{
	if (fw->klog)
		klog = fwts_file_open_and_read(fw->klog);
	else
		klog = fwts_klog_read();

	if (klog == NULL) {
		fwts_log_error(fw, "Cannot read kernel log.");
		return FWTS_ERROR;
	}
	return FWTS_OK;
}

static int acpiinfo_deinit(fwts_framework *fw)
{
	fwts_klog_free(klog);

	return FWTS_OK;
}

static int acpiinfo_test1(fwts_framework *fw)
{	
	char *test = "General ACPI information check.";
	int errors = 0;

	fwts_log_info(fw, "This test checks the output of the in-kernel ACPI CA against common "
		 "error messages that indicate a bad interaction with the bios, including "
		 "those that point at AML syntax errors.");

	if (fwts_klog_scan(fw, klog, acpiinfo_check, NULL, NULL, &errors)) {
		fwts_log_error(fw, "failed to scan kernel log.");
		return FWTS_ERROR;
	}

	if (errors > 0)
		fwts_log_info(fw, "Found %d errors in kernel log.", errors);
	else
		fwts_passed(fw, test);

	return FWTS_OK;
}

static fwts_framework_tests acpiinfo_tests[] = {
	acpiinfo_test1,
	NULL
};

static fwts_framework_ops acpiinfo_ops = {
	acpiinfo_headline,
	acpiinfo_init,	
	acpiinfo_deinit,
	acpiinfo_tests
};

FWTS_REGISTER(acpiinfo, &acpiinfo_ops, FWTS_TEST_EARLY, FWTS_BATCH);

#endif
