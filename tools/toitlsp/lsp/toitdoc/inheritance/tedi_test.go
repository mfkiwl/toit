// Code generated by tedi; DO NOT EDIT.

package inheritance

import (
	"github.com/jstroem/tedi"
	"os"
	"testing"
)

func TestMain(m *testing.M) {
	t := tedi.New(m)

	// TestLabels:
	t.TestLabel("regression")
	t.TestLabel("unit")
	t.TestLabel("integration")

	os.Exit(t.Run())
}
