#!/bin/bash

# Setup script for migrating from requirements.txt to uv
# This script will help you set up the uv environment for the Master node

echo "Setting up uv environment for ACNode Master..."

# Check if uv is installed
if ! command -v uv &> /dev/null; then
    echo "Error: uv is not installed. Please install it first:"
    echo "curl -LsSf https://astral.sh/uv/install.sh | sh"
    exit 1
fi

# Initialize uv project (this will create uv.lock)
echo "Initializing uv project..."
uv sync

# Install dependencies
echo "Installing dependencies..."
uv pip install -e .

# Install dev dependencies (optional)
echo "Installing development dependencies..."
uv pip install -e ".[dev]"

echo ""
echo "Setup complete! You can now run the master with:"
echo "  uv run python master.py --debug --dbfile sample-keydb.txt"
echo ""
echo "Or activate the virtual environment with:"
echo "  uv shell"
echo ""
echo "To add new dependencies, edit pyproject.toml and run:"
echo "  uv sync" 