default: build

.PHONY: default build flash setup clean

build:
	uv run west build -b nucleo_h723zg .

clean:
	uv run west build -b nucleo_h723zg -t pristine .

flash:
	uv run west flash

setup:
	uv venv --python=3.12
	uv sync
	uv run west update
	uv run west zephyr-export
	uv run west packages pip --install
	uv run west sdk install
