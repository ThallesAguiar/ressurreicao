package cutscene

import (
	"os"
	"strings"
	"testing"

	"github.com/xeeynamo/sotn-decomp/tools/sotn-assets/assets"
)

func TestBuildLatin1Text(t *testing.T) {
	t.Setenv("VERSION", "us")
	dir := t.TempDir()
	text := "script:\n  - [TEXT, \"Voc\u00ea n\u00e3o \u00e9 miser\u00e1vel.\"]\n"
	if err := os.WriteFile(assetPath(dir, "test"), []byte(text), 0644); err != nil {
		t.Fatal(err)
	}
	if err := (&handler{}).Build(assets.BuildArgs{AssetDir: dir, SrcDir: dir, Name: "test"}); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(sourcePath(dir, "test"))
	if err != nil {
		t.Fatal(err)
	}
	want := "'V','o','c','\\xEA',' ','n','\\xE3','o',' ','\\xE9',' ','m','i','s','e','r','\\xE1','v','e','l','.',"
	if !strings.Contains(string(data), want) {
		t.Fatalf("expected single-byte Latin-1 glyphs, got %s", data)
	}
}
