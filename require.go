// Copyright 2020 The Godror Authors
//
// SPDX-License-Identifier: UPL-1.0 OR Apache-2.0

//go:build require
// +build require

package godror

import (
	_ "github.com/travelliu/godror_ob/odpi/embed"   // ODPI-C
	_ "github.com/travelliu/godror_ob/odpi/include" // ODPI-C
	_ "github.com/travelliu/godror_ob/odpi/src"     // ODPI-C
)
