export interface AnimationParameter {
  name: string;
  boolean: boolean;
  numberInput?: boolean;
  value: number;
  defaultValue: number;
  minimum: number;
  maximum: number;
}

export function parseParameters(source: string, overrides: AnimationParameter[] = []): AnimationParameter[] {
  const parameters: AnimationParameter[] = [];
  let header = true;
  for (const raw of source.split(/\r?\n/)) {
    const line = raw.trim();
    if (line.startsWith('-- @parameter')) {
      if (!header) throw new Error('Parameter declarations must be at the top of the animation');
      if (!line.startsWith('-- @parameter ')) throw new Error('Invalid parameter declaration');
      const parts = line.slice(14).split('//')[0].split(',').map(part => part.trim());
      const [name, type, initial, low, high] = parts;
      if (!/^[A-Za-z][A-Za-z0-9_ ]{0,31}$/.test(name ?? '') || parameters.some(p => p.name === name)) throw new Error('Parameter names must be unique, 1–32 ASCII letters, digits, spaces or underscores, starting with a letter');
      if (parameters.length >= 16) throw new Error('An animation supports at most 16 parameters');
      if ((type === 'Checkbox' || type === 'Boolean') && parts.length === 3 && ['true', 'false'].includes(initial)) {
        parameters.push({ name, boolean: true, numberInput: false, value: initial === 'true' ? 1 : 0, defaultValue: initial === 'true' ? 1 : 0, minimum: 0, maximum: 1 });
      } else if ((type === 'Slider' || type === 'Number') && parts.length === 5 && [initial, low, high].every(value => /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(value) && Number.isFinite(Number(value)) && Math.abs(Number(value)) <= 1e9)) {
        const value = Number(initial), minimum = Number(low), maximum = Number(high);
        if (minimum >= maximum || value < minimum || value > maximum) throw new Error(`Invalid range or default for parameter ${name}`);
        parameters.push({ name, boolean: false, numberInput: type === 'Number', value, defaultValue: value, minimum, maximum });
      } else throw new Error(`Invalid parameter declaration for ${name}`);
    } else if (line && (!line.startsWith('--') || line.startsWith('--['))) header = false;
  }
  return updateParameters(parameters, overrides);
}

// Apply a validated patch atomically; omitted values retain their current value.
export function updateParameters(parameters: AnimationParameter[], overrides: AnimationParameter[]): AnimationParameter[] {
  const result = parameters.map(p => ({ ...p }));
  const names = new Set<string>();
  for (const override of overrides) {
    const parameter = result.find(p => p.name === override.name);
    if (!parameter || names.has(override.name)) throw new Error(`Unknown or duplicate parameter: ${override.name}`);
    if (Boolean(override.boolean) !== parameter.boolean || !Number.isFinite(override.value) || override.value < parameter.minimum || override.value > parameter.maximum || (parameter.boolean && ![0, 1].includes(override.value))) throw new Error(`Invalid value for parameter ${override.name}`);
    names.add(override.name); parameter.value = override.value;
  }
  return result;
}
