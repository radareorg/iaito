import xml.etree.ElementTree as ET
import subprocess
import sys

# Define the translation function
def translate_text(input_text):
    lang = "Catalan"
    """
    Calls an external program to translate the input text.

    Args:
        input_text (str): The text to translate.

    Returns:
        str: The translated text.
    """
    try:
        # Replace 'translation_program' with the actual program/command
        process = subprocess.Popen(
            ['/usr/local/bin/ai', '"translate this sentence to '+lang+', the output must be just the text, the context for this translation is the Iaito program, which is the graphical interface for radare2, a reverse engineering tool for analyzing and debugging programs"'],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        stdout, stderr = process.communicate(input=input_text)

        if process.returncode != 0:
            raise RuntimeError(f"Translation program error: {stderr.strip()}")

        print("<==")
        print(input_text)
        print("==>")
        print(stdout)
        return stdout.strip()
    except Exception as e:
        print(f"Error during translation: {e}", file=sys.stderr)
        return input_text  # Fallback to the original text if an error occurs

# Define the main function to parse and modify the XML file
def process_ts_file(input_file, output_file):
    """
    Parses the .ts file, translates the <source> content, and writes the output.

    Args:
        input_file (str): Path to the input .ts file.
        output_file (str): Path to save the modified .ts file.
    """
    tree = ET.parse(input_file)
    root = tree.getroot()

    for context in root.findall('context'):
        for message in context.findall('message'):
            source = message.find('source')
            if source is not None and source.text:
                original_text = source.text
                translated_text = translate_text(original_text)
                source.text = translated_text

    # Save the updated XML to the output file
    tree.write(output_file, encoding='utf-8', xml_declaration=True)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python script.py input.ts output.ts", file=sys.stderr)
        sys.exit(1)

    input_ts = sys.argv[1]
    output_ts = sys.argv[2]

    process_ts_file(input_ts, output_ts)
    print(f"Translated file saved to {output_ts}")

