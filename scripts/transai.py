#!/usr/bin/env python
import xml.etree.ElementTree as ET
import subprocess
import sys

def remove_lines_with_dashes(input_string):
    lines = input_string.splitlines()
    filtered_lines = [line for line in lines if "----------" not in line]
    return "\n".join(filtered_lines).strip()

# Define the translation function
def translate_text(input_text, existing_translation, language):
    """
    Calls an external program to translate the input text, using existing translation if provided.

    Args:
        input_text (str): The text to translate.
        existing_translation (str): The existing translation to provide context.

    Returns:
        str: The translated text.
    """
    inspiration = ""
    if existing_translation and existing_translation != "":
        inspiration = ". Take this original text as inspiration: '"+existing_translation+"'"
    try:
        # Replace 'translation_program' with the actual program/command
        process = subprocess.Popen(
            ['/usr/local/bin/ai', '--', '"translate this sentence to '+language+', the output must be just the text, the context for this translation is the Iaito program, which is the graphical interface for radare2, a reverse engineering tool for analyzing and debugging programs'+inspiration+'"'],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        print('"translate this sentence to '+language+', the output must be just the text, the context for this translation is the Iaito program, which is the graphical interface for radare2, a reverse engineering tool for analyzing and debugging programs'+inspiration+'"')
        input_data = f"{input_text}"
        stdout, stderr = process.communicate(input=input_data)

        if process.returncode != 0:
            raise RuntimeError(f"Translation program error: {stderr.strip()}")

        print(stdout)
        return remove_lines_with_dashes(stdout)
    except Exception as e:
        print(f"Error during translation: {e}", file=sys.stderr)
        return input_text  # Fallback to the original text if an error occurs

# Define the function to find existing translation
def find_existing_translation(existing_tree, source_text):
    """
    Finds the translation for a given source text in an existing .ts file.

    Args:
        existing_tree (ElementTree): Parsed XML tree of the existing .ts file.
        source_text (str): The source text to look for.

    Returns:
        str: The existing translation if found, else an empty string.
    """
    for context in existing_tree.findall('context'):
        for message in context.findall('message'):
            source = message.find('source')
            translation = message.find('translation')
            if source is not None and source.text == source_text and translation is not None:
                return translation.text
    return ""

# Define the main function to parse and modify the XML file
def process_ts_file(input_file, output_file, existing_file, language):
    """
    Parses the .ts file, adds a <translation> node with translated content, and writes the output.

    Args:
        input_file (str): Path to the input .ts file.
        output_file (str): Path to save the modified .ts file.
        existing_file (str): Path to an existing .ts file to find previous translations.
    """
    tree = ET.parse(input_file)
    root = tree.getroot()

    existing_tree = ET.parse(existing_file)

    for context in root.findall('context'):
        for message in context.findall('message'):
            source = message.find('source')
            if source is not None and source.text:
                original_text = source.text
                existing_translation = find_existing_translation(existing_tree, original_text)
                translated_text = translate_text(original_text, existing_translation, language)
                # Check if <translation> node exists, if not create it
                translation = message.find('translation')
                if translation is None:
                    translation = ET.SubElement(message, 'translation')
                translation.text = translated_text
                if 'type' in translation.attrib:
                    del translation.attrib['type']

    # Save the updated XML to the output file
    tree.write(output_file, encoding='utf-8', xml_declaration=True)

if __name__ == "__main__":
    if len(sys.argv) <= 4:
        print("Usage: python script.py input.ts output.ts existing.ts (Language)", file=sys.stderr)
        sys.exit(1)

    input_ts = sys.argv[1]
    output_ts = sys.argv[2]
    existing_ts = sys.argv[3]
    if len(sys.argv) <= 5:
        language = sys.argv[4]

    process_ts_file(input_ts, output_ts, existing_ts, language)
    print(f"Translated file saved to {output_ts}")

