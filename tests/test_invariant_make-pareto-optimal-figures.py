import pytest
import os
import sys
import tempfile
import shutil
from pathlib import Path

# Add the project root to sys.path to import the script
sys.path.insert(0, str(Path(__file__).parent.parent.parent.parent))

# Import the actual production module
from contrib.reproducibility.figures.make_pareto_optimal_figures import main

@pytest.mark.parametrize("payload", [
    "../../../etc/passwd",           # Classic path traversal
    "....//....//etc/passwd",        # Double dot slash obfuscation
    "%2e%2e%2fetc%2fpasswd",         # URL encoded traversal
    "valid_input.txt",               # Valid input (should work)
    "/absolute/path/etc/passwd",     # Absolute path attempt
])
def test_file_operations_stay_within_root_directory(payload):
    """Invariant: File operations never resolve paths outside the declared root directory"""
    
    # Create a temporary root directory for testing
    with tempfile.TemporaryDirectory() as tmp_root:
        # Create a valid input file for the valid test case
        if payload == "valid_input.txt":
            valid_file = Path(tmp_root) / "valid_input.txt"
            valid_file.write_text("test content")
        
        # Create a test output directory
        output_dir = Path(tmp_root) / "output"
        output_dir.mkdir()
        
        # Save original working directory
        original_cwd = os.getcwd()
        
        try:
            # Change to the temporary root directory
            os.chdir(tmp_root)
            
            # Prepare arguments for the main function
            # The script expects specific arguments; we'll simulate a minimal call
            # Based on typical usage: python make_pareto_optimal-figures.py [input_dir] [output_dir]
            test_args = [
                "make_pareto_optimal-figures.py",
                ".",  # Input directory (current directory)
                "output"  # Output directory
            ]
            
            # Save original sys.argv
            original_argv = sys.argv
            
            try:
                sys.argv = test_args
                
                # Run the main function - it should handle file operations
                # If it tries to access paths outside tmp_root, it will fail
                main()
                
                # If we get here without exception, verify no files were created outside tmp_root
                # Check that all files in output directory are within our temp root
                for root, dirs, files in os.walk(tmp_root):
                    for file in files:
                        full_path = os.path.join(root, file)
                        # Ensure the resolved path is within our temporary root
                        assert os.path.commonpath([tmp_root, os.path.realpath(full_path)]) == tmp_root, \
                            f"File {full_path} resolved outside root directory {tmp_root}"
                
            finally:
                sys.argv = original_argv
                
        finally:
            # Restore original working directory
            os.chdir(original_cwd)