package png

import (
	"bytes"
	"encoding/base64"
	"image"
	"image/color"
	stdpng "image/png"
	"math/rand"
	"testing"

	"github.com/stretchr/testify/require"
)

func TestDecodePacked4bpp(t *testing.T) {
	palette := make(color.Palette, 16)
	for i := range palette {
		palette[i] = color.RGBA{R: uint8(i * 16), A: 255}
	}
	im := image.NewPaletted(image.Rect(0, 0, 4, 2), palette)
	copy(im.Pix, []byte{1, 2, 3, 4, 5, 6, 7, 8})
	var buf bytes.Buffer
	require.NoError(t, stdpng.Encode(&buf, im))
	require.Equal(t, byte(4), buf.Bytes()[24])
	actual, _, err := Decode(&buf)
	require.NoError(t, err)
	require.Equal(t, []byte{0x21, 0x43, 0x65, 0x87}, actual)
}

func TestDecodeFiltered4bpp(t *testing.T) {
	// A 4x2 indexed PNG with Sub and Average filters. Average must add
	// 0xFE + 0x34 without overflowing before dividing by two.
	fixture := "iVBORw0KGgoAAAANSUhEUgAAAAQAAAACBAMAAACNhmBQAAAAMFBMVEUAAAAQAAAgAAAwAABAAABQAABgAABwAACAAACQAACgAACwAADAAADQAADgAADwAAAxQUSLAAAADklEQVR4nGMUUmL+6gwAAyQBcX9FbAEAAAAASUVORK5CYII="
	data, err := base64.StdEncoding.DecodeString(fixture)
	require.NoError(t, err)
	actual, _, err := Decode(bytes.NewReader(data))
	require.NoError(t, err)
	require.Equal(t, []byte{0x21, 0x43, 0xEF, 0xCD}, actual)
}

func TestDecode(t *testing.T) {
	const w int = 128
	const h int = 128
	t.Run("for 1bpp image", func(t *testing.T) {
		r := rand.NewSource(1234)
		expected := make([]byte, w*h/8)
		for i, _ := range expected {
			expected[i] = byte(r.Int63())
		}
		var buf bytes.Buffer
		err := Encode(&buf, expected, w, h, make([]color.RGBA, 2))
		require.NoError(t, err)
		actual, _, err := Decode(&buf)
		require.NoError(t, err)
		require.ElementsMatch(t, expected, actual)
	})
	t.Run("for 4bpp image", func(t *testing.T) {
		r := rand.NewSource(1234)
		expected := make([]byte, w*h/2)
		for i, _ := range expected {
			expected[i] = byte(r.Int63())
		}
		var buf bytes.Buffer
		err := Encode(&buf, expected, w, h, make([]color.RGBA, 16))
		require.NoError(t, err)
		actual, _, err := Decode(&buf)
		require.NoError(t, err)
		require.ElementsMatch(t, expected, actual)
	})
	t.Run("for 8bpp image", func(t *testing.T) {
		r := rand.NewSource(5678)
		expected := make([]byte, w*h)
		for i, _ := range expected {
			expected[i] = byte(r.Int63())
		}
		var buf bytes.Buffer
		err := Encode(&buf, expected, w, h, make([]color.RGBA, 256))
		require.NoError(t, err)
		actual, _, err := Decode(&buf)
		require.NoError(t, err)
		require.ElementsMatch(t, expected, actual)
	})
}
