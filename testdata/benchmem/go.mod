module github.com/godror/testdata/benchmem

go 1.23.0

toolchain go1.24.1

require (
	github.com/travelliu/godror_ob v0.40.1
	github.com/prometheus/procfs v0.11.1
	gonum.org/v1/gonum v0.14.0
)

require (
	github.com/go-logfmt/logfmt v0.6.0 // indirect
	github.com/godror/knownpb v0.3.0 // indirect
	golang.org/x/exp v0.0.0-20250506013437-ce4c2cf36ca6 // indirect
	golang.org/x/sys v0.22.0 // indirect
	google.golang.org/protobuf v1.36.6 // indirect
)

replace github.com/travelliu/godror_ob => ../../

replace github.com/travelliu/godror_ob/knownpb => ../../knownpb
